//go:build libusb

package usb

import (
	"github.com/google/gousb"
	"testing"
)

// Descriptor-only tests: no context, device handle or USB operations are used.
func TestDiscoveryFiltersAndConfigurationSelection(t *testing.T) {
	alt := gousb.InterfaceSetting{Number: 7, Alternate: 2, Class: gousb.ClassVendorSpec, SubClass: 0xfe}
	d := &gousb.DeviceDesc{Vendor: apple, Configs: map[int]gousb.ConfigDesc{2: {Number: 2, Interfaces: []gousb.InterfaceDesc{{Number: 7, AltSettings: []gousb.InterfaceSetting{alt}}}}}}
	if !candidate(d) {
		t.Fatal("Apple mux interface not discovered")
	}
	d.Vendor = 0x1234
	if candidate(d) {
		t.Fatal("unrelated USB vendor discovered")
	}
	d.Vendor = apple
	if _, _, ok := qtSetting(&gousb.Device{Desc: d}); ok {
		t.Fatal("mux interface mistaken for capture")
	}
	alt.SubClass = 0x2a
	d.Configs[5] = gousb.ConfigDesc{Number: 5, Interfaces: []gousb.InterfaceDesc{{Number: 7, AltSettings: []gousb.InterfaceSetting{alt}}}}
	config, setting, ok := qtSetting(&gousb.Device{Desc: d})
	if !ok || config != 5 || setting.Number != 7 || setting.Alternate != 2 {
		t.Fatal("capture descriptor selection failed")
	}
}
