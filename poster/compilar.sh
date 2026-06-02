#!/bin/bash
# Compila el poster desde la carpeta poster-template
cd "$(dirname "$0")"
pdflatex -interaction=nonstopmode lab4-poster.tex
pdflatex -interaction=nonstopmode lab4-poster.tex
echo "Listo: lab4-poster.pdf"
