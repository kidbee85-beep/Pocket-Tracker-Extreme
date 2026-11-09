//go:build libusb

package usb

import (
	"context"
	"errors"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/google/gousb"
)

const apple = 0x05ac

func candidate(desc *gousb.DeviceDesc) bool {
	if desc.Vendor != apple {
		return false
	}
	for _, cfg := range desc.Configs {
		for _, iface := range cfg.Interfaces {
			for _, alt := range iface.AltSettings {
				if alt.Class == gousb.ClassVendorSpec && (alt.SubClass == 0xfe || alt.SubClass == 0x2a) {
					return true
				}
			}
		}
	}
	return false
}
func normalize(s string) string { return strings.ToLower(strings.ReplaceAll(s, "-", "")) }
func List() ([]Device, error) {
	c := gousb.NewContext()
	defer c.Close()
	handles, openErr := c.OpenDevices(candidate)
	result := make([]Device, 0, len(handles))
	for _, d := range handles {
		d.ControlTimeout = 500 * time.Millisecond
		id, err := d.SerialNumber()
		if err == nil && id != "" {
			name, _ := d.Product()
			if name == "" {
				name = "Apple iPhone / iPad"
			}
			result = append(result, Device{id, name})
		}
		d.Close()
	}
	sort.Slice(result, func(i, j int) bool { return result[i].ID < result[j].ID })
	return result, openErr
}
func find(c *gousb.Context, id string, location *gousb.DeviceDesc) (*gousb.Device, error) {
	handles, err := c.OpenDevices(func(desc *gousb.DeviceDesc) bool {
		if !candidate(desc) {
			return false
		}
		if location == nil {
			return true
		}
		if desc.Bus != location.Bus || len(desc.Path) != len(location.Path) {
			return false
		}
		for i, p := range location.Path {
			if desc.Path[i] != p {
				return false
			}
		}
		return true
	})
	var found *gousb.Device
	for _, d := range handles {
		d.ControlTimeout = 750 * time.Millisecond
		serial, serialErr := d.SerialNumber()
		if serialErr == nil && normalize(serial) == normalize(id) && found == nil {
			found = d
		} else {
			d.Close()
		}
	}
	if found != nil {
		return found, nil
	}
	if err != nil {
		return nil, err
	}
	return nil, errors.New("selected iPhone is not connected or cannot be opened")
}
func qtSetting(d *gousb.Device) (int, gousb.InterfaceSetting, bool) {
	keys := make([]int, 0, len(d.Desc.Configs))
	for key := range d.Desc.Configs {
		keys = append(keys, key)
	}
	sort.Ints(keys)
	for _, key := range keys {
		cfg := d.Desc.Configs[key]
		for _, iface := range cfg.Interfaces {
			for _, alt := range iface.AltSettings {
				if alt.Class == gousb.ClassVendorSpec && alt.SubClass == 0x2a {
					return cfg.Number, alt, true
				}
			}
		}
	}
	return 0, gousb.InterfaceSetting{}, false
}

type connection struct {
	ctx            *gousb.Context
	dev            *gousb.Device
	cfg            *gousb.Config
	iface          *gousb.Interface
	in             *gousb.InEndpoint
	out            *gousb.OutEndpoint
	id             string
	originalConfig int
	location       *gousb.DeviceDesc
	activated      bool
	closed         bool
}

// Open changes USB mode only as the direct consequence of an explicit record.
// It never resets the phone, detaches unrelated drivers, or changes phone UI.
func Open(ctx context.Context, id string) (Transport, error) {
	c := &connection{ctx: gousb.NewContext(), id: id}
	var err error
	c.dev, err = find(c.ctx, id, nil)
	if err != nil {
		c.ctx.Close()
		return nil, err
	}
	c.location = c.dev.Desc
	c.originalConfig, err = c.dev.ActiveConfigNum()
	if err != nil {
		c.Close()
		return nil, fmt.Errorf("reading original USB configuration: %w", err)
	}
	config, setting, ok := qtSetting(c.dev)
	if !ok {
		c.activated = true
		// Re-enumeration can make the control transfer itself report disconnect.
		_, activationErr := c.dev.Control(0x40, 0x52, 0, 2, nil)
		c.dev.Close()
		c.dev = nil
		deadline := time.Now().Add(10 * time.Second)
		for time.Now().Before(deadline) && ctx.Err() == nil {
			select {
			case <-ctx.Done():
			case <-time.After(150 * time.Millisecond):
			}
			d, openErr := find(c.ctx, id, c.location)
			if openErr != nil {
				continue
			}
			config, setting, ok = qtSetting(d)
			if ok {
				c.dev = d
				break
			}
			d.Close()
		}
		if !ok {
			c.Close()
			if ctx.Err() != nil {
				return nil, ctx.Err()
			}
			return nil, fmt.Errorf("iPhone did not expose the experimental audio endpoint; unlock it, use a data cable and accept Trust if shown (USB activation result: %v)", activationErr)
		}
	}
	c.cfg, err = c.dev.Config(config)
	if err != nil {
		c.Close()
		return nil, fmt.Errorf("selecting iPhone audio configuration: %w", err)
	}
	c.iface, err = c.cfg.Interface(setting.Number, setting.Alternate)
	if err != nil {
		c.Close()
		return nil, fmt.Errorf("claiming iPhone audio interface (another application may use it): %w", err)
	}
	for _, ep := range setting.Endpoints {
		if ep.TransferType != gousb.TransferTypeBulk {
			continue
		}
		if ep.Direction == gousb.EndpointDirectionIn && c.in == nil {
			c.in, err = c.iface.InEndpoint(ep.Number)
		}
		if ep.Direction == gousb.EndpointDirectionOut && c.out == nil {
			c.out, err = c.iface.OutEndpoint(ep.Number)
		}
		if err != nil {
			c.Close()
			return nil, err
		}
		// Clear endpoint halt, as in the upstream QuickTime transport.
		if _, err = c.dev.Control(0x02, 0x01, 0, uint16(ep.Address), nil); err != nil {
			c.Close()
			return nil, fmt.Errorf("initializing audio endpoint: %w", err)
		}
	}
	if c.in == nil || c.out == nil {
		c.Close()
		return nil, errors.New("iPhone audio interface has no bulk input/output pair")
	}
	return c, nil
}
func (c *connection) Read(ctx context.Context, b []byte) (int, error) {
	n, err := c.in.ReadContext(ctx, b)
	if err != nil && ctx.Err() != nil {
		return n, ctx.Err()
	}
	return n, err
}
func (c *connection) Write(ctx context.Context, b []byte) error {
	n, err := c.out.WriteContext(ctx, b)
	if err != nil {
		return err
	}
	if n != len(b) {
		return errors.New("short USB protocol write")
	}
	return nil
}
func (c *connection) Close() error {
	if c.closed {
		return nil
	}
	c.closed = true
	if c.iface != nil {
		c.iface.Close()
	}
	if c.cfg != nil {
		c.cfg.Close()
	}
	var cleanupErr error
	if c.dev == nil && c.activated {
		c.dev, _ = find(c.ctx, c.id, c.location)
	}
	if c.dev != nil {
		if c.activated {
			_, cleanupErr = c.dev.Control(0x40, 0x52, 0, 0, nil)
			c.dev.Close()
			c.dev = nil
			// The disable request can re-enumerate too. Restore the original
			// configuration on a fresh handle, bounded independently of SIGTERM.
			deadline := time.Now().Add(time.Second)
			for time.Now().Before(deadline) {
				time.Sleep(100 * time.Millisecond)
				d, err := find(c.ctx, c.id, c.location)
				if err == nil {
					c.dev = d
					break
				}
			}
		}
		if c.dev != nil {
			if c.originalConfig > 0 {
				cfg, err := c.dev.Config(c.originalConfig)
				if err == nil {
					cfg.Close()
					cleanupErr = nil
				} else {
					cleanupErr = err
				}
			}
			c.dev.Close()
		}
	}
	c.ctx.Close()
	return cleanupErr
}
