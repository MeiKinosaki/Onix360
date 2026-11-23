package main

import (
	"os"

	"github.com/abemedia/go-cabinet/lzx"
)

func main() {
	// CAB-compatible regular LZX reference stream generated independently
	// from WX360's libmspack decoder. A short final frame is intentional.
	const plain = "WX360 independent LZX encoder reference vector"
	enc, err := lzx.NewEncoder(15)
	if err != nil { panic(err) }
	compressed, err := enc.Encode(nil, []byte(plain))
	if err != nil { panic(err) }
	if err := os.MkdirAll("../../tests/data", 0755); err != nil { panic(err) }
	if err := os.WriteFile("../../tests/data/lzx_reference.bin", compressed, 0644); err != nil { panic(err) }
	if err := os.WriteFile("../../tests/data/lzx_reference.expected", []byte(plain), 0644); err != nil { panic(err) }
}
