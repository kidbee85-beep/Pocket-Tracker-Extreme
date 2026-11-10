//go:build !libusb

package usb

import (
	"context"
	"errors"
)

var unavailable = errors.New("this helper was built without USB support (release builds require -tags libusb)")

func List() ([]Device, error)                         { return []Device{}, unavailable }
func Open(context.Context, string) (Transport, error) { return nil, unavailable }
