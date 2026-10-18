package main

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestSafeEnvelope_CatchesPanic(t *testing.T) {
	result := safeEnvelope(func() string {
		panic("boom")
	})
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "panic: boom")
}

func TestSafeEnvelope_PanicInNestedCallIsAlsoCaught(t *testing.T) {
	// Confirms the panic doesn't need to originate directly inside the
	// closure passed to safeEnvelope — only recover() itself must be
	// called directly by the deferred function (see safeEnvelope's doc
	// comment); panics from arbitrarily deep call chains are still caught.
	deeper := func() string { panic("deep boom") }
	middle := func() string { return deeper() }

	result := safeEnvelope(middle)
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "panic: deep boom")
}

func TestLoginImpl_InvalidArgsJSONIsAnEnvelopeNotAPanic(t *testing.T) {
	result := loginImpl("not json")
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "INVALID_ARGUMENT")
}

func TestLoginImpl_UnsupportedModeIsAnEnvelope(t *testing.T) {
	withTempConfigDir(t)
	result := loginImpl(`{"mode":"carrier-pigeon"}`)
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "INVALID_ARGUMENT")
}

func TestLogoutImpl_Success(t *testing.T) {
	withTempConfigDir(t)
	result := logoutImpl()
	assert.Contains(t, result, `"ok":true`)
	assert.Contains(t, result, "logged_out")
}

func TestSearchFilesImpl_NotLoggedInReturnsUnauthenticatedEnvelope(t *testing.T) {
	withTempConfigDir(t)
	result := searchFilesImpl(`{"query":"title contains 'x'","pageSize":10,"pageToken":""}`)
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "UNAUTHENTICATED")
}

func TestTrashFileImpl_InvalidArgsJSONIsAnEnvelope(t *testing.T) {
	withTempConfigDir(t)
	result := trashFileImpl("{not valid json")
	assert.Contains(t, result, `"ok":false`)
	assert.Contains(t, result, "INVALID_ARGUMENT")
}

func TestGdaFreeString_DoesNotCrashOnANilPointer(t *testing.T) {
	// gda_free_string itself needs cgo (so it can't be called from a _test.go
	// file at all); this exercises the one piece of its contract that is
	// plain Go: freeing a nil *C.char-equivalent must be a safe no-op. The
	// actual free(3) call is exercised end-to-end by the C++ FFI Bridge
	// module's own tests once libgodrive.a is linked in.
	gda_free_string(nil)
}
