#!/bin/sh
# The Vercel build: the engine compiled on Vercel's build image, the dashboard
# generated from the committed data, and that one file published as the site's
# root page. vercel.json runs this as the build command with `public` as the
# output directory, so nothing else is served.
#
#   make dashboard CXX=g++  ->  dashboard.html  ->  public/index.html
#
# Vercel's build image is Amazon Linux 2023 with gcc-c++, make and git among its
# pre-installed packages (docs/DEPLOYMENT.md, "Vercel"), so the C++ build needs
# no install step. The checks at the end are the ones CI's build job makes
# before it publishes to GitHub Pages: the benchmark table must be the committed
# CSV field for field, and the page must load nothing from anywhere. Run it
# locally with `sh tools/vercel_build.sh`; it writes only build/, dashboard.html
# and public/.
set -eu

# The Makefile lists sources with find(1). Vercel's package set brings it in; a
# bare Amazon Linux container does not have it, so install rather than fail.
command -v find >/dev/null 2>&1 || dnf install -y findutils

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
echo "vercel_build: $(g++ --version | head -1), $jobs jobs"
make -j"$jobs" dashboard CXX=g++

mkdir -p public
cp dashboard.html public/index.html

# Evidence checks, as in .github/workflows/deploy.yml.
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  # The build writes only ignored files, so a changed tracked file means a
  # step rewrote evidence before publishing.
  git diff --exit-code --stat HEAD
fi
rows=$(awk -F, 'NR > 1 && NF >= 7 { printf "%s{\"zones\":%s,\"brute\":%s,\"quad\":%s,\"rtree\":%s,\"qgain\":%s,\"rgain\":%s,\"cand\":%s}", sep, $1, $2, $3, $4, $5, $6, $7; sep = "," }' bench/results/index_scaling.csv)
test -n "$rows"
grep -qF "\"experiment\":[$rows]" public/index.html
# Self-contained: no script, stylesheet, image or frame fetched from anywhere,
# and no request made at runtime.
if grep -qE '<(script|link|img|iframe|object|embed|video|audio)[^>]*(src|href)=|@import|url\(|fetch\(|XMLHttpRequest|WebSocket|EventSource|importScripts' public/index.html; then
  echo "vercel_build: public/index.html is not self-contained" >&2
  exit 1
fi

echo "vercel_build: public/index.html, $(wc -c < public/index.html | tr -d ' ') bytes, from commit ${VERCEL_GIT_COMMIT_SHA:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}"
