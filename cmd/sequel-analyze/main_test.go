package main

import (
	"bytes"
	"strings"
	"testing"

	sequel "github.com/zfogg/sequel/go"
)

func TestCLIEmitsSanitizedVersionedResult(t *testing.T) {
	var out, stderr bytes.Buffer
	status := run([]string{"-dialect", "postgresql", "-format"}, strings.NewReader("SELECT 'private' FROM records WHERE id=$1;"), &out, &stderr)
	if status != 0 {
		t.Fatalf("exit %d: %s", status, stderr.String())
	}
	if strings.Contains(out.String(), "private") {
		t.Fatal("CLI leaked literal")
	}
	a, err := sequel.UnmarshalAnalysis(out.Bytes())
	if err != nil {
		t.Fatal(err)
	}
	if a.Fingerprint == "" || a.Formatted == "" || len(a.Binds) != 1 {
		t.Fatalf("incomplete result: %#v", a)
	}
}

func TestCLIFailuresRemainMachineReadable(t *testing.T) {
	for _, tt := range []struct {
		args          []string
		query, status string
	}{
		{nil, "SELECT 'private", "malformed"},
		{[]string{"-max-input-bytes", "8"}, "SELECT 'private' FROM t", "limit"},
		{[]string{"-max-work-units", "1"}, "SELECT 1", "limit"},
	} {
		var out, stderr bytes.Buffer
		if status := run(tt.args, strings.NewReader(tt.query), &out, &stderr); status != 1 {
			t.Fatalf("exit %d: %s", status, stderr.String())
		}
		a, err := sequel.UnmarshalAnalysis(out.Bytes())
		if err != nil {
			t.Fatalf("output %s: %v", out.String(), err)
		}
		if a.Status != tt.status || a.Text != "" || strings.Contains(out.String(), "private") {
			t.Fatalf("unsafe failure output: %#v", a)
		}
	}
}

func TestCLIRejectsPositionalSQLAndUnknownFlags(t *testing.T) {
	for _, args := range [][]string{{"SELECT private"}, {"-unknown"}} {
		var out, stderr bytes.Buffer
		if status := run(args, strings.NewReader(""), &out, &stderr); status != 2 {
			t.Fatalf("exit %d", status)
		}
		if out.Len() != 0 {
			t.Fatal("invalid CLI emitted JSON")
		}
	}
}
