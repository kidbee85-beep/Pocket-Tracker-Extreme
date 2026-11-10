package usb

import "context"

type Device struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}
type Transport interface {
	Read(context.Context, []byte) (int, error)
	Write(context.Context, []byte) error
	Close() error
}
