package sequel

/*
#include <stdint.h>
#include <stdbool.h>
*/
import "C"

import (
	"context"
	"runtime/cgo"
)

//export sequelGoCancelled
func sequelGoCancelled(handle C.uintptr_t) C.bool {
	ctx := cgo.Handle(handle).Value().(context.Context)
	return C.bool(ctx.Err() != nil)
}
