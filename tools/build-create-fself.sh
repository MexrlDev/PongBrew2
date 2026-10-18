#!/bin/sh
set -e
apk add --no-cache git >/dev/null 2>&1
mkdir -p /tmp/gopath/src/github.com/OpenOrbis
cd /tmp/gopath/src/github.com/OpenOrbis
rm -rf create-fself
git clone --depth=1 https://github.com/OpenOrbis/create-fself.git >/dev/null 2>&1
cd create-fself
rm -f go.mod go.sum
cd cmd/create-fself
echo "--- building ---"
GO111MODULE=off GOPATH=/tmp/gopath CGO_ENABLED=0 go build -o /work/tools/create-fself .
echo "--- result ---"
ls -la /work/tools/create-fself
file /work/tools/create-fself || true
echo "--- create-fself usage ---"
/work/tools/create-fself 2>&1 || true
