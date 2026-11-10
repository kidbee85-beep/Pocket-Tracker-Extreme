package main

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/binary"
	"encoding/pem"
	"errors"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/danielpaulus/go-ios/ios"
	"howett.net/plist"
)

const testUDID = "00000001-0000000000000002"

type fakeBackend struct {
	validation []error
	pairing    []error
	pairCalls  int
}

func (fake *fakeBackend) find(string) (ios.DeviceEntry, error) {
	return ios.DeviceEntry{}, nil
}
func (fake *fakeBackend) validate(ios.DeviceEntry) error {
	if len(fake.validation) == 0 {
		return errors.New("not paired")
	}
	value := fake.validation[0]
	fake.validation = fake.validation[1:]
	return value
}
func (fake *fakeBackend) pair(ios.DeviceEntry) error {
	fake.pairCalls++
	if len(fake.pairing) == 0 {
		return errors.New("not paired")
	}
	value := fake.pairing[0]
	fake.pairing = fake.pairing[1:]
	return value
}

func TestPairLoopNormalTrustRetryAndValidation(t *testing.T) {
	fake := &fakeBackend{
		validation: []error{errors.New("missing pair record"), errors.New("missing pair record"), nil},
		pairing:    []error{errors.New("Please accept the PairingDialog on the device"), nil},
	}
	var states []string
	err := pairLoop(context.Background(), testUDID, fake, func(state, _, _ string) { states = append(states, state) }, time.Millisecond)
	if err != nil || fake.pairCalls != 2 || strings.Join(states, ",") != "connecting,pairing,trust,pairing,paired" {
		t.Fatalf("unexpected normal trust lifecycle: err=%v calls=%d states=%v", err, fake.pairCalls, states)
	}
}

func TestPairLoopDoesNotRequestNewIdentityWhileLocked(t *testing.T) {
	fake := &fakeBackend{validation: []error{errors.New("PasswordProtected"), nil}}
	var states []string
	err := pairLoop(context.Background(), testUDID, fake, func(state, _, _ string) { states = append(states, state) }, time.Millisecond)
	if err != nil || fake.pairCalls != 0 || strings.Join(states, ",") != "connecting,unlock,paired" {
		t.Fatalf("locked identity handling failed: err=%v calls=%d states=%v", err, fake.pairCalls, states)
	}
}

func TestPairLoopHonorsDenial(t *testing.T) {
	fake := &fakeBackend{validation: []error{errors.New("missing")}, pairing: []error{errors.New("UserDeniedPairing")}}
	var states []string
	err := pairLoop(context.Background(), testUDID, fake, func(state, _, _ string) { states = append(states, state) }, time.Millisecond)
	if err == nil || fake.pairCalls != 1 || states[len(states)-1] != "error" {
		t.Fatalf("pairing denial was not terminal")
	}
}

func TestPairLoopNeverReportsPairedAfterOnlySave(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Millisecond)
	defer cancel()
	fake := &fakeBackend{validation: []error{errors.New("missing"), errors.New("InvalidHostID")}, pairing: []error{nil}}
	paired := false
	err := pairLoop(ctx, testUDID, fake, func(state, _, _ string) { paired = paired || state == "paired" }, time.Millisecond)
	if !errors.Is(err, context.DeadlineExceeded) || paired {
		t.Fatal("save-only exchange must not be reported as paired")
	}
}

func TestSerialAndArguments(t *testing.T) {
	if !sameSerial(testUDID, "000000010000000000000002") || sameSerial(testUDID, "00000003-0000000000000002") {
		t.Fatal("wrong USB serial identity matching")
	}
	for _, value := range []string{"", "../phone", "*", "1234", testUDID + "\n"} {
		if validSerial(value) {
			t.Fatalf("invalid serial accepted: %q", value)
		}
	}
	for _, args := range [][]string{
		nil,
		{"pair", "--udid", testUDID, "--socket", "/var/run/usbmuxd"},
		{"pair", "--udid", testUDID, "--socket", "/tmp/mux.sock", "--timeout", "91"},
		{"pair", "--udid", "malicious\nprivatekey", "--socket", "/tmp/mux.sock"},
	} {
		var output bytes.Buffer
		if run(args, &output) == 0 || strings.Contains(output.String(), "privatekey") {
			t.Fatal("bad arguments accepted or reflected")
		}
	}
}

func privateListener(t *testing.T) (net.Listener, string) {
	t.Helper()
	directory, err := os.MkdirTemp("/tmp", "pte-pair-test-")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = os.RemoveAll(directory) })
	socket := filepath.Join(directory, "mux.sock")
	listener, err := net.Listen("unix", socket)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = listener.Close() })
	return listener, socket
}

func TestSocketIsolation(t *testing.T) {
	_, socket := privateListener(t)
	if err := validateSocket(socket); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(filepath.Dir(socket), 0755); err != nil {
		t.Fatal(err)
	}
	if validateSocket(socket) == nil {
		t.Fatal("public parent accepted")
	}
	for _, value := range []string{"/var/run/usbmuxd", "/run/usbmuxd", "tcp://localhost:27015", "relative.sock", "/tmp/../tmp/mux.sock"} {
		if validateSocket(value) == nil {
			t.Fatalf("non-private socket accepted: %s", value)
		}
	}
}

// Mock only the standard USBMux/lockdown messages. No USB hardware is touched,
// no real phone pair record is read, and no TLS compatibility is claimed here.
func TestRealGoIOSNormalPairExchangeAgainstLocalMock(t *testing.T) {
	listener, socket := privateListener(t)
	key, err := rsa.GenerateKey(rand.Reader, 1024)
	if err != nil {
		t.Fatal(err)
	}
	publicKey := pem.EncodeToMemory(&pem.Block{Type: "RSA PUBLIC KEY", Bytes: x509.MarshalPKCS1PublicKey(&key.PublicKey)})
	var mu sync.Mutex
	var saved []byte
	pairRequests := 0
	var connections []net.Conn
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			connection, err := listener.Accept()
			if err != nil {
				return
			}
			mu.Lock()
			connections = append(connections, connection)
			mu.Unlock()
			go func(conn net.Conn) {
				defer conn.Close()
				for {
					request, tag, err := readMux(conn)
					if err != nil {
						return
					}
					switch request["MessageType"] {
					case "ListDevices":
						writeMux(conn, tag, map[string]any{"DeviceList": []any{
							map[string]any{"DeviceID": 4, "Properties": map[string]any{"SerialNumber": testUDID, "ConnectionType": "Network"}},
							map[string]any{"DeviceID": 7, "Properties": map[string]any{"SerialNumber": testUDID, "ConnectionType": "USB"}},
						}})
					case "ReadBUID":
						writeMux(conn, tag, map[string]any{"BUID": "PTE-MOCK-BUID"})
					case "ReadPairRecord":
						mu.Lock()
						record := append([]byte(nil), saved...)
						mu.Unlock()
						if len(record) == 0 {
							writeMux(conn, tag, map[string]any{"MessageType": "Result", "Number": 2})
						} else {
							writeMux(conn, tag, map[string]any{"PairRecordData": record})
						}
					case "SavePairRecord":
						mu.Lock()
						saved = append([]byte(nil), request["PairRecordData"].([]byte)...)
						mu.Unlock()
						writeMux(conn, tag, map[string]any{"MessageType": "Result", "Number": 0})
					case "Connect":
						if request["DeviceID"] != uint64(7) {
							return
						}
						writeMux(conn, tag, map[string]any{"MessageType": "Result", "Number": 0})
						for {
							message, err := readLockdown(conn)
							if err != nil {
								return
							}
							switch message["Request"] {
							case "GetValue":
								if message["Key"] == "DevicePublicKey" {
									writeLockdown(conn, map[string]any{"Value": publicKey})
								} else {
									writeLockdown(conn, map[string]any{"Value": "00:11:22:33:44:55"})
								}
							case "Pair":
								// Normal pairing sends public certificates, never host private keys.
								record := message["PairRecord"].(map[string]any)
								if _, leaked := record["HostPrivateKey"]; leaked {
									return
								}
								mu.Lock()
								pairRequests++
								count := pairRequests
								mu.Unlock()
								if count == 1 {
									writeLockdown(conn, map[string]any{"Error": "PairingDialogResponsePending"})
								} else {
									writeLockdown(conn, map[string]any{"Request": "Pair", "EscrowBag": []byte("mock-secret-escrow")})
								}
							case "StartSession":
								writeLockdown(conn, map[string]any{"Request": "StartSession", "SessionID": "mock-session", "EnableSessionSSL": false})
							case "StopSession":
								writeLockdown(conn, map[string]any{"Request": "StopSession"})
								return
							default:
								return
							}
						}
					default:
						return
					}
				}
			}(connection)
		}
	}()
	t.Cleanup(func() {
		_ = listener.Close()
		<-done
		mu.Lock()
		defer mu.Unlock()
		for _, conn := range connections {
			_ = conn.Close()
		}
	})
	var output bytes.Buffer
	if code := run([]string{"pair", "--udid", strings.ReplaceAll(testUDID, "-", ""), "--socket", socket, "--timeout", "10"}, &output); code != 0 {
		t.Fatalf("mock normal pairing failed: %s", output.String())
	}
	if !strings.Contains(output.String(), `"state":"trust"`) || !strings.Contains(output.String(), `"state":"paired"`) {
		t.Fatal("missing trust or paired status")
	}
	for _, secret := range []string{"PRIVATE KEY", "CERTIFICATE", "mock-secret-escrow", "PairRecordData", "HostID"} {
		if strings.Contains(output.String(), secret) {
			t.Fatalf("secret appeared in diagnostics: %s", secret)
		}
	}
	mu.Lock()
	defer mu.Unlock()
	if pairRequests != 2 || len(saved) == 0 {
		t.Fatal("expected trust retry and persisted pair record")
	}
}

func readMux(r io.Reader) (map[string]any, uint32, error) {
	var header [4]uint32
	if err := binary.Read(r, binary.LittleEndian, &header); err != nil {
		return nil, 0, err
	}
	if header[0] < 16 || header[0] > 1024*1024 {
		return nil, 0, errors.New("bad mock mux length")
	}
	data := make([]byte, header[0]-16)
	if _, err := io.ReadFull(r, data); err != nil {
		return nil, 0, err
	}
	var value map[string]any
	_, err := plist.Unmarshal(data, &value)
	return value, header[3], err
}
func writeMux(w io.Writer, tag uint32, value map[string]any) {
	data, _ := plist.Marshal(value, plist.XMLFormat)
	_ = binary.Write(w, binary.LittleEndian, [4]uint32{uint32(16 + len(data)), 1, 8, tag})
	_, _ = w.Write(data)
}
func readLockdown(r io.Reader) (map[string]any, error) {
	var length uint32
	if err := binary.Read(r, binary.BigEndian, &length); err != nil {
		return nil, err
	}
	if length > 1024*1024 {
		return nil, errors.New("bad mock lockdown length")
	}
	data := make([]byte, length)
	if _, err := io.ReadFull(r, data); err != nil {
		return nil, err
	}
	var value map[string]any
	_, err := plist.Unmarshal(data, &value)
	return value, err
}
func writeLockdown(w io.Writer, value map[string]any) {
	data, _ := plist.Marshal(value, plist.XMLFormat)
	_ = binary.Write(w, binary.BigEndian, uint32(len(data)))
	_, _ = w.Write(data)
}

func TestHelperTimeoutProcess(t *testing.T) {
	if os.Getenv("PTE_PAIR_TIMEOUT_CHILD") == "1" {
		os.Exit(run([]string{"pair", "--udid", testUDID, "--socket", os.Getenv("PTE_PAIR_TEST_SOCKET"), "--timeout", "1"}, os.Stdout))
	}
	listener, socket := privateListener(t)
	accepted := make(chan net.Conn, 1)
	go func() { connection, _ := listener.Accept(); accepted <- connection }()
	command := exec.Command(os.Args[0], "-test.run=^TestHelperTimeoutProcess$")
	command.Env = append(os.Environ(), "PTE_PAIR_TIMEOUT_CHILD=1", "PTE_PAIR_TEST_SOCKET="+socket)
	started := time.Now()
	output, err := command.CombinedOutput()
	if err == nil || time.Since(started) > 4*time.Second || !bytes.Contains(output, []byte(`"error":"pairing_timeout"`)) {
		t.Fatalf("blocked USBMux read was not bounded: %s (%v)", output, err)
	}
	select {
	case conn := <-accepted:
		if conn != nil {
			conn.Close()
		}
	case <-time.After(time.Second):
		t.Fatal("mock server was not contacted")
	}
}
