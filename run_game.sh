#!/bin/bash
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

# DB 준비는 서버를 백그라운드로 띄우기 전에 먼저 처리한다.
# 그래야 MySQL 관리자 비밀번호 입력 프롬프트가 터미널에 정상 표시된다.
if [ ! -f "$ROOT_DIR/.env.server" ]; then
    echo "DB 설정 파일이 없습니다. 로컬 DB와 테스트 계정을 먼저 생성합니다."
    bash "$ROOT_DIR/scripts/setup_database.sh" || exit 1
fi

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
