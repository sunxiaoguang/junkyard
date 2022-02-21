package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
	"time"
)

type log struct {
	ts          time.Time
	level       string
	file        string
	fileAndLine string
	line        string
}

type linesAndBytes struct {
	Lines int
	Bytes int
}

type stats struct {
	Start          time.Time
	End            time.Time
	Elapsed        float64
	Bytes          int
	Lines          int
	ByLevel        map[string]linesAndBytes
	ByFile         map[string]linesAndBytes
	ByFileAndLine  map[string]linesAndBytes
	LinesPerSecond int
	BytesPerSecond int
}

func (s *stats) Finalize() *stats {
	s.Elapsed = s.End.Sub(s.Start).Seconds()
	elapsed := int(s.Elapsed)
	if elapsed != 0 {
		s.LinesPerSecond = s.Lines / elapsed
		s.BytesPerSecond = s.Bytes / elapsed
	}
	return s
}

func (s *stats) collect(l log) {
	if s.Start.IsZero() {
		s.Start = l.ts
	}
	s.End = l.ts
	s.Bytes += len(l.line)
	s.Lines += 1
	c, _ := s.ByLevel[l.level]
	c.Lines += 1
	c.Bytes += len(l.line)
	s.ByLevel[l.level] = c
	c, _ = s.ByFile[l.file]
	c.Lines += 1
	c.Bytes += len(l.line)
	s.ByFile[l.file] = c
	c, _ = s.ByFileAndLine[l.fileAndLine]
	c.Lines += 1
	c.Bytes += len(l.line)
	s.ByFileAndLine[l.fileAndLine] = c
}

func extract(line string) (log, error) {
	line = line[:len(line)-1][1:]
	parts := strings.Split(line, "] [")
	ts, err := time.Parse("2006/01/02 15:04:05.999 +08:00", parts[0])
	return log{
		ts:          ts,
		level:       parts[1],
		file:        strings.Split(parts[2], ":")[0],
		fileAndLine: parts[2],
		line:        line,
	}, err
}

func analyze(path string) (*stats, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}

	defer file.Close()

	scanner := bufio.NewScanner(file)

	s := &stats{
		ByLevel:       make(map[string]linesAndBytes),
		ByFile:        make(map[string]linesAndBytes),
		ByFileAndLine: make(map[string]linesAndBytes),
	}

	for scanner.Scan() {
		l, err := extract(scanner.Text())
		if err != nil {
			return nil, err
		}
		s.collect(l)
	}

	return s.Finalize(), scanner.Err()
}

func main() {
	dump := make(map[string]*stats)
	for _, file := range os.Args[1:] {
		if s, err := analyze(file); err != nil {
			panic(err)
		} else {
			dump[file] = s
		}
	}
	if b, err := json.MarshalIndent(dump, "", "    "); err != nil {
		panic(err)
	} else {
		fmt.Println(string(b))
	}
}
