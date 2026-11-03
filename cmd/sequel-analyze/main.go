// Command sequel-analyze reads UTF-8 SQL from stdin and emits sanitized,
// schema-versioned analysis JSON. It does not execute SQL.
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"

	sequel "github.com/zfogg/sequel/go"
)

func run(args []string, input io.Reader, output, errors io.Writer) int {
	flags := flag.NewFlagSet("sequel-analyze", flag.ContinueOnError)
	flags.SetOutput(errors)
	dialect := flags.String("dialect", "generic", "SQL dialect")
	format := flags.Bool("format", false, "include formatted sanitized SQL")
	omit := flags.Bool("omit-text", false, "omit query text")
	omitMaps := flags.Bool("omit-maps", false, "omit source maps")
	omitBinds := flags.Bool("omit-binds", false, "omit bind descriptors")
	maxInput := flags.Uint64("max-input-bytes", 0, "input byte budget (zero uses 1 GiB)")
	maxOutput := flags.Uint64("max-output-bytes", 0, "total native result byte budget")
	maxMemory := flags.Uint64("max-memory-bytes", 0, "native working memory budget")
	maxWork := flags.Uint64("max-work-units", 0, "native work budget (zero is unlimited)")
	maxText := flags.Int("max-query-text-bytes", 0, "sanitized query text limit")
	if flags.Parse(args) != nil {
		return 2
	}
	if flags.NArg() != 0 {
		fmt.Fprintln(errors, "sequel-analyze reads SQL from stdin")
		return 2
	}
	limit := uint64(sequel.MaxInputBytes)
	if *maxInput > 0 && *maxInput < limit {
		limit = *maxInput
	}
	query, err := io.ReadAll(io.LimitReader(input, int64(limit)+1))
	if err != nil {
		fmt.Fprintln(errors, "cannot read SQL input")
		return 2
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	result := sequel.Analyze(string(query), sequel.AnalysisOptions{
		Dialect: sequel.Dialect(*dialect), Format: *format, OmitQueryText: *omit,
		OmitSourceMaps: *omitMaps, OmitBindDescriptors: *omitBinds,
		MaxInputBytes: *maxInput, MaxOutputBytes: *maxOutput, MaxMemoryBytes: *maxMemory, MaxWorkUnits: *maxWork,
		MaxQueryTextBytes: *maxText, Context: ctx,
	})
	encoded, err := sequel.MarshalAnalysis(result)
	if err != nil {
		fmt.Fprintln(errors, "cannot encode SQL analysis result")
		return 2
	}
	if _, err = fmt.Fprintln(output, string(encoded)); err != nil {
		return 2
	}
	if result.Status != "ok" {
		return 1
	}
	return 0
}

func main() { os.Exit(run(os.Args[1:], os.Stdin, os.Stdout, os.Stderr)) }
