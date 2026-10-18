package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"golang.org/x/oauth2"
)

const configDirName = "google-drive-agent"

// configDirOverride lets tests point token/client storage at a t.TempDir()
// instead of the real ~/.config, without changing the production code path
// (Plan §8.4's "no real ~/.config" testing requirement).
var configDirOverride string

func configDir() (string, error) {
	if configDirOverride != "" {
		return configDirOverride, nil
	}
	home, err := os.UserHomeDir()
	if err != nil {
		return "", fmt.Errorf("resolving home directory: %w", err)
	}
	return filepath.Join(home, ".config", configDirName), nil
}

func tokenStorePath() (string, error) {
	dir, err := configDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "token.json"), nil
}

func clientConfigPath() (string, error) {
	dir, err := configDir()
	if err != nil {
		return "", err
	}
	return filepath.Join(dir, "client.json"), nil
}

// LoadToken reads the persisted OAuth2 token. A missing file is not an
// error: it returns (nil, nil) so callers can distinguish "never logged
// in" from an actual I/O failure.
func LoadToken() (*oauth2.Token, error) {
	path, err := tokenStorePath()
	if err != nil {
		return nil, err
	}
	data, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return nil, nil
		}
		return nil, fmt.Errorf("reading token store: %w", err)
	}
	var tok oauth2.Token
	if err := json.Unmarshal(data, &tok); err != nil {
		return nil, fmt.Errorf("parsing token store: %w", err)
	}
	return &tok, nil
}

// SaveToken persists the OAuth2 token with 0600 permissions (Plan §6.4
// step 5 / §11).
func SaveToken(tok *oauth2.Token) error {
	path, err := tokenStorePath()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return fmt.Errorf("creating config directory: %w", err)
	}
	data, err := json.MarshalIndent(tok, "", "  ")
	if err != nil {
		return fmt.Errorf("marshaling token: %w", err)
	}
	if err := os.WriteFile(path, data, 0600); err != nil {
		return fmt.Errorf("writing token store: %w", err)
	}
	return os.Chmod(path, 0600) // belt-and-suspenders: WriteFile's mode is subject to umask
}

// DeleteToken removes the persisted token; used by gda_logout. Removing a
// token that doesn't exist is not an error.
func DeleteToken() error {
	path, err := tokenStorePath()
	if err != nil {
		return err
	}
	if err := os.Remove(path); err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("removing token store: %w", err)
	}
	return nil
}

// persistingTokenSource wraps an oauth2.TokenSource so that whenever the
// underlying token is silently refreshed, the new token is written back to
// disk, and a refresh failure due to `invalid_grant` is translated into a
// clear "re-run login" failure instead of a generic OAuth error (Plan §6.4,
// "on refresh token expiry").
type persistingTokenSource struct {
	base oauth2.TokenSource
	last *oauth2.Token
}

func (p *persistingTokenSource) Token() (*oauth2.Token, error) {
	tok, err := p.base.Token()
	if err != nil {
		if strings.Contains(err.Error(), "invalid_grant") {
			return nil, newAppError("UNAUTHENTICATED", 0,
				"Google refused to refresh the stored token (invalid_grant); run `login` again.", false)
		}
		return nil, newAppError("INTERNAL", 0, fmt.Sprintf("refreshing OAuth token: %v", err), false)
	}
	if p.last == nil || tok.AccessToken != p.last.AccessToken {
		if saveErr := SaveToken(tok); saveErr != nil {
			return nil, saveErr
		}
		p.last = tok
	}
	return tok, nil
}
