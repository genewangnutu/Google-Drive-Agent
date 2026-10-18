package main

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"golang.org/x/oauth2"
)

// withTempConfigDir points configDir() at a t.TempDir() for the duration of
// the test, per Plan §8.1's "no real ~/.config" testing requirement.
func withTempConfigDir(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	old := configDirOverride
	configDirOverride = dir
	t.Cleanup(func() { configDirOverride = old })
	return dir
}

func TestSaveAndLoadToken_RoundTrip(t *testing.T) {
	withTempConfigDir(t)

	want := &oauth2.Token{AccessToken: "access-123", RefreshToken: "refresh-456", TokenType: "Bearer"}
	require.NoError(t, SaveToken(want))

	got, err := LoadToken()
	require.NoError(t, err)
	require.NotNil(t, got)
	assert.Equal(t, want.AccessToken, got.AccessToken)
	assert.Equal(t, want.RefreshToken, got.RefreshToken)
}

func TestSaveToken_FilePermissionsAre0600(t *testing.T) {
	dir := withTempConfigDir(t)

	require.NoError(t, SaveToken(&oauth2.Token{AccessToken: "x"}))

	path := filepath.Join(dir, "token.json")
	info, err := os.Stat(path)
	require.NoError(t, err)
	assert.Equal(t, os.FileMode(0600), info.Mode().Perm())
}

func TestLoadToken_MissingFileReturnsNilNil(t *testing.T) {
	withTempConfigDir(t)

	tok, err := LoadToken()
	assert.NoError(t, err)
	assert.Nil(t, tok)
}

func TestDeleteToken_RemovesFileAndIsIdempotent(t *testing.T) {
	withTempConfigDir(t)

	require.NoError(t, SaveToken(&oauth2.Token{AccessToken: "x"}))
	require.NoError(t, DeleteToken())

	tok, err := LoadToken()
	require.NoError(t, err)
	assert.Nil(t, tok)

	// Deleting again (nothing to delete) must not be an error.
	assert.NoError(t, DeleteToken())
}

func TestPersistingTokenSource_SavesRefreshedToken(t *testing.T) {
	withTempConfigDir(t)

	refreshed := &oauth2.Token{AccessToken: "new-access", RefreshToken: "new-refresh"}
	src := &persistingTokenSource{base: oauth2.StaticTokenSource(refreshed)}

	got, err := src.Token()
	require.NoError(t, err)
	assert.Equal(t, "new-access", got.AccessToken)

	saved, err := LoadToken()
	require.NoError(t, err)
	require.NotNil(t, saved)
	assert.Equal(t, "new-access", saved.AccessToken)
}

type invalidGrantTokenSource struct{}

func (invalidGrantTokenSource) Token() (*oauth2.Token, error) {
	return nil, &oauth2.RetrieveError{ErrorCode: "invalid_grant", ErrorDescription: "Token has been expired or revoked."}
}

func TestPersistingTokenSource_InvalidGrantIsClearFailure(t *testing.T) {
	withTempConfigDir(t)

	src := &persistingTokenSource{base: invalidGrantTokenSource{}}
	_, err := src.Token()
	require.Error(t, err)

	appErr := mapError(err)
	assert.Equal(t, "UNAUTHENTICATED", appErr.Code)
	assert.Contains(t, appErr.Message, "login")
}
