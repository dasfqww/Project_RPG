# Asset Studio: 연결 기반 0.1

엔진에 의존하지 않는 에셋 편집 도구의 첫 실행 단위다. Node 24+와 Blender Python으로 동작하며 npm 의존성 설치가 필요 없다. 저장소 내 `Tools/AssetStudio`에 독립 배치했고 Unreal 모듈이나 `Tools/Production` 큐를 참조하지 않는다. 이후 데스크톱 UI와 작업 관리자가 이 연결부를 호출한다.

현재 제공하는 기능은 **새 Blender 창에서 작업용 사본 열기, 장면 조회, 정적 오브젝트의 로컬 위치·크기 변경, 단순 Principled 재질 값 변경, 체크포인트 저장, Codex 계정 연결 진단**이다. 자연어 자동 편집, MCP 노출, 데스크톱 UI, 생성 공급자, 장기 작업 큐, 리깅·리타게팅, 엔진 내보내기는 아직 구현하지 않았다.

## 실행

아래 명령은 이 디렉터리에서 실행한다. `node`, Blender 및 Codex는 사용자의 실제 실행 파일 경로로 지정할 수 있다. 실행 파일 경로를 소스에 고정하거나 사용자 인증 파일을 읽지 않는다.

```powershell
node src/cli.mjs launch --blender 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --workspace 'C:\AssetStudioWorkspace' --input 'C:\Assets\Monster.blend'
```

`--input`을 생략하면 Blender 기본 장면을 연다. 이는 AI 생성 기능이 아니다. 첫 연결부의 입력 형식은 `.blend`다. 항상 별도의 보이는 Blender 프로세스를 시작하며, 이미 열려 있는 사용자의 Blender에 연결하거나 그 장면을 교체하지 않는다. 입력 파일의 자동 Python 실행은 비활성화한다.

결과로 받은 `connection_file`을 다음 명령에 사용한다. 파일에는 해당 로컬 세션의 인증용 토큰이 있으므로 게시하지 않는다. 토큰은 명령 출력에 포함하지 않으며 프로세스는 `127.0.0.1`의 임의 포트에만 바인딩한다. 같은 OS 사용자로 실행되는 임의 프로그램까지 격리하는 보안 경계는 아니다.

```powershell
node src/cli.mjs inspect --connection 'C:\AssetStudioWorkspace\sessions\<session-id>\connection.json'
node src/cli.mjs apply --connection 'C:\AssetStudioWorkspace\sessions\<session-id>\connection.json' --request 'C:\AssetStudioWorkspace\request.json'
node src/cli.mjs codex-status --codex 'C:\path\to\codex.exe'
```

`codex-status`는 공식 app-server의 `initialize → initialized → account/read(refreshToken=false)`만 사용한다. 모델 요청·로그인/로그아웃·추가 크레딧 구매를 수행하지 않는다. ChatGPT 인증 감지는 모델 사용 가능 여부나 추가 지출 차단 확인과 다르다. 요금제 식별자는 서버 응답 그대로 표시하며 상품명으로 해석하지 않는다. 이메일·토큰·서버 stderr는 출력/보존하지 않는다. [공식 App Server 문서](https://learn.chatgpt.com/docs/app-server).

## 명령 계약

`inspect` 응답의 `result.blender_session_id`, `result.scene_revision`, `result.objects[].id`를 사용한다. 오브젝트 ID는 **해당 Blender 세션 안에서만** 유효하며 이름을 변경해도 유지된다. 파일 로드·undo/redo 후에는 다시 조회한다. 선택 상태는 편집 대상을 대신하지 않는다.

```json
{
  "protocol_version": 1,
  "operation_id": "edit-001",
  "blender_session_id": "<inspect에서 받은 세션 ID>",
  "expected_scene_revision": 4,
  "operation": "object.transform",
  "target_ids": ["<inspect에서 받은 오브젝트 ID>"],
  "params": { "location": [1, 2, 3], "scale": [1, 1, 1] }
}
```

| operation | params | target_ids |
|---|---|---|
| `scene.inspect` | `{}` | `[]` |
| `object.transform` | `location` 및/또는 `scale`: 유한한 수 3개 | 오브젝트 1개 |
| `material.apply_profile` | `slot`: 0 기반 인덱스, `base_color`: RGBA 4개, `roughness`, `metallic`: 0~1 | 메시 1개 |
| `checkpoint.save` | `{}` | `[]` |

재질 명령의 색상은 Blender 셰이더의 선형 RGBA 값이다. 이 단계는 숫자 재질 값을 적용하며 스타일 프로필 라이브러리나 텍스처 재생성을 구현하지 않는다. 기존 텍스처가 연결된 입력·복합 셰이더는 거절하고 원본 재질을 복사해 대상에만 배정한다. 첫 재질 슬롯을 추가할 때도 공유 메시의 다른 오브젝트를 보존한다.

애니메이션·드라이버·제약이 걸린 오브젝트의 위치 변경은 전용 연산이 필요하므로 거절한다. 재질 편집은 기존 메시와 액션을 보존한다. 이는 임의의 몬스터 리그 변형 품질을 검증했다는 의미는 아니다. 편집 모드·애니메이션 재생·렌더링 중에는 쓰기를 거절한다.

## 중복 요청·변경 충돌·저장

- 네트워크 처리와 `bpy` 접근은 Blender 메인 이벤트 루프의 비차단 타이머에서 실행한다. 임의 Python 실행 명령은 노출하지 않는다. [Blender 타이머 설명](https://docs.blender.org/api/current/bpy.app.timers.html).
- 장면 변경 알림은 보수적으로 revision을 증가시킨다. 요청의 세션/revision이 다르면 실행하지 않는다. 거절 후에는 최신 장면을 검토하고 새 operation ID로 명령을 준비한다. 모든 사용자 변경 종류를 완벽히 식별한다는 보장은 없으며, 수동/자동 제어권 UI는 후속이다.
- 입력은 작업 디렉터리의 `working.blend`로 저장한 후 편집한다. 원본 `.blend`를 덮어쓰지 않는다. 현재 파일을 외부 경로로 바꿨으면 새 작업 세션으로 열어야 한다.
- 편집 전 `operations/<id>.prepared.json`과 `checkpoints/before-<id>.blend`를 저장한다. 편집 후 `checkpoints/<id>.blend`와 해시를 포함한 실행 영수증을 저장한다. 저장 파일에는 기존 외부 텍스처/라이브러리 의존성이 남을 수 있다. 휴대 가능한 에셋 패키징은 후속이다.
- 같은 작업 세션에서 성공한 요청을 그대로 재전송하면 기존 영수증을 돌려준다. 같은 ID의 다른 내용은 `ID_REUSED`로 거절한다. 영수증은 원래 실행의 증거이며 현재 장면 상태를 뜻하지 않는다.
- 전송 후 응답이 사라지면 클라이언트는 `effect_state=unknown`을 반환하고 자동 재시도하지 않는다. Blender가 살아 있으면 동일 요청으로 영수증을 조회할 수 있다. 작업 중 저장 실패가 나면 추가 편집을 차단한다.
- Blender 종료 후에는 저널/체크포인트를 사용자가 확인하고 선택한 `.blend`에서 새 세션을 시작한다. 자동 복구·영속 큐·정확히 한 번 실행을 보장하지 않는다.
- 세션당 성공/미확정 쓰기 기록 1,000개까지 유지하고 조용히 기록을 제거하지 않는다. 일반 작업 결과는 사용자의 버전 자료이므로 검증용 임시 파일과 달리 자동 삭제하지 않는다.

## 검증

```powershell
node --test tests/transport.test.mjs
node scripts/verify.mjs --blender 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --codex 'C:\path\to\codex.exe' --proof-root 'C:\AssetStudioChecks'
```

검증 러너는 통신/진단 계약, 실제 Blender 데이터와 `.blend` 재개방, 별도 보이는 Blender의 소켓 편집·중복 요청·잘못된 토큰·저장 해시, 실제 CLI 조회를 검사한다. `--codex`가 있으면 계정 진단도 수행하며 모델 요청은 없다. 성공·실패 모두 자기 프로세스를 종료하고, 소유 표식·절대 경로·링크 경계를 확인한 뒤 자기 Temp 하위만 정리한다. 작은 결과 JSON과 입력 해시만 남긴다. 종료/경계 확인이 실패하면 정리 예외를 기록한다.

실제 실행 확인 환경: Windows, Node 24.12.0, Blender 5.2.0 LTS, Codex CLI 0.167.0. 대규모 장면·임의 파일 손상·전체 undo 동작·24시간 운영·GPU 렌더 품질·실제 비인간형 리그는 아직 검증 범위 밖이다. 첫 버전은 장면 오브젝트 2,000개와 요청/응답 1 MiB로 제한한다.
