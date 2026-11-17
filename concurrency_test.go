//go:build cgo && !nomupdf

package pdftext

import (
	"bytes"
	"fmt"
	"os"
	"strings"
	"sync"
	"testing"
)

// storyHTML is a few pages of what a Word document's rendition carries:
// headings, styled runs, aligned paragraphs, a table, and CJK text (shaped
// through HarfBuzz, whose MuPDF glue is shared by every context).
func storyHTML() string {
	var b strings.Builder
	for i := 1; i <= 40; i++ {
		fmt.Fprintf(&b, "<h2>Section %d</h2><p style=\"text-align:justify\">Clause %d: the <b>fee</b> is <i>two</i> "+
			"times the <u>base</u>, paid each quarter in arrears. 物件概要 学芸大学 %d.</p>", i, i, i)
		if i%10 == 0 {
			b.WriteString("<table><tr><td>Rate</td><td>0.75</td></tr><tr><td>Cap</td><td>1,000</td></tr></table>")
		}
	}
	return b.String()
}

// TestConcurrentUse runs Story layouts, text extraction and page renders in
// several goroutines at once, each with its own documents, as a server
// serving several requests does. Before every context got MuPDF's locks, two
// Story layouts at once crashed the process inside fz_place_story.
func TestConcurrentUse(t *testing.T) {
	pdf, err := os.ReadFile("testdata/columns.pdf")
	if err != nil {
		t.Fatal(err)
	}
	html := storyHTML()
	want, err := StoryPDF(html, "", A4, Box{36, 36, 559, 806})
	if err != nil {
		t.Fatal(err)
	}
	const workers, rounds = 8, 6
	var wg sync.WaitGroup
	errs := make(chan error, workers*rounds)
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func(w int) {
			defer wg.Done()
			for r := 0; r < rounds; r++ {
				if err := oneRound(w+r, html, want, pdf); err != nil {
					errs <- fmt.Errorf("worker %d round %d: %w", w, r, err)
				}
			}
		}(w)
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		t.Error(err)
	}
}

// oneRound is one of three uses, by turn: a Story laid out (the same bytes
// every time), a PDF's words read, or a page drawn.
func oneRound(i int, html string, want, pdf []byte) error {
	switch i % 3 {
	case 0:
		got, err := StoryPDF(html, "", A4, Box{36, 36, 559, 806})
		if err != nil {
			return err
		}
		if !bytes.Equal(got, want) {
			return fmt.Errorf("story: %d bytes, %d alone", len(got), len(want))
		}
	case 1:
		d, err := Open(pdf)
		if err != nil {
			return err
		}
		defer d.Close()
		for n := 0; n < d.Pages(); n++ {
			if _, err := d.Page(n); err != nil {
				return err
			}
		}
	default:
		d, err := Open(pdf)
		if err != nil {
			return err
		}
		defer d.Close()
		if _, err := d.Render(0, 144); err != nil {
			return err
		}
	}
	return nil
}
