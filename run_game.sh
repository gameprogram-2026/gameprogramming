#!/bin/bash
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 기존 서버 종료
if lsof -i :7777 -t >/dev/null 2>&1; then
    echo "기존 서버 종료 중..."
    kill $(lsof -i :7777 -t) 2>/dev/null
    sleep 1
fi

# 서버 실행
echo "서버 시작 중..."
bash "$ROOT_DIR/run_server.sh" > "$ROOT_DIR/build/bin/server.log" 2>&1 &
SERVER_PID=$!

# 서버 준비 대기
for i in {1..10}; do
    sleep 1
    if lsof -i :7777 -t >/dev/null 2>&1; then
        echo "서버 준비 완료 (PID: $SERVER_PID)"
        break
    fi
    echo "서버 대기 중... ($i/10)"
done

# 클라이언트 실행
echo "클라이언트 실행 중..."
open "$ROOT_DIR/build/bin/DeadZoneClient"

echo ""
echo "종료하려면 Ctrl+C 또는 아래 명령 실행:"
echo "  kill $SERVER_PID"
