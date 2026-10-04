# Build the proof document:  make -C proof        (output: proof/build/main.pdf)
.PHONY: all clean
all:
	latexmk main.tex
clean:
	latexmk -C main.tex
	rm -rf build
