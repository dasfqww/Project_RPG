# 운영툴 실행과 유지보수

2026-11-05 · S1 개인 로그인/기본 조회/로그 추적, S2 보상 영수증·정산 조사, S3 개별 아이템 보상과 S4 런타임 감시·알림 구현. [상세 계약](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md)의 기간 제재/해제·공지/점검·백업 수집/복구는 후속 구현 범위다.

## 구현 경계

- [Operations.Core](../Backend/ProjectRpg.Operations.Core/)는 업무 모델, 결과, capability, 읽기/원자 지급 포트와 유스케이스만 가진다. ASP.NET, Npgsql, Serilog, 게임 Backend 참조가 없다. 권한 확인 후 입력 검증과 포트 호출 순서로 처리한다.
- [Operations/Hosting](../Backend/ProjectRpg.Backend/Operations/Hosting/)은 DI·인증·관리망·요청 제한을 조립한다. [Web](../Backend/ProjectRpg.Backend/Operations/Web/)은 입력/커서/응답 변환을 담당하며 [Razor 화면](../Backend/ProjectRpg.Backend/Pages/Ops/)과 JSON API가 같은 유스케이스를 쓴다.
- [읽기 어댑터](../Backend/ProjectRpg.Backend/Operations/Infrastructure/Reads/)는 저장된 데이터만 관측한다. PostgreSQL은 READ ONLY / REPEATABLE READ, 명령 3초·요청 5초 제한이다. 조회가 만료 리스 정리나 성장 보상 반영을 호출하지 않는다. 패널 간 동일 snapshot은 보장하지 않는다.
- 운영 인증은 게임 Bearer와 별도다. GitHub 숫자 ID 한 개를 허용하고 PKCE/state·CSRF, Secure/HttpOnly cookie, 서버 세션을 사용한다. 세션은 유휴 30분/절대 8시간이며 로그아웃과 AuthEpoch 변경으로 무효화한다.
- 로그 수집/저장 구현은 Infrastructure에만 둔다. 요청 번호 → HTTP trace → PostgreSQL 읽기 child span을 연결한다. 정산 worker는 별도 관측 포트로 원래 세션/거래 요청 ID·시도 trace를 남긴다. 게임 서버/UE 전체 사건 계측은 제공하지 않으며 같은 HTTP trace가 worker까지 전파됐다고 표시하지 않는다.

인터페이스를 추가할 때는 교체할 외부 의존성 또는 실제 유스케이스 경계가 있어야 한다. 새로운 지급 기능을 읽기 포트나 PageModel에 넣지 않는다. 여러 책임을 묶는 공통 repository/manager로 합치지 않는다.

## 활성화와 배포 입력

기본값 `Operations:Enabled=false`에서는 `/ops` 라우트·운영 인증·로그 writer를 등록하지 않는다. 활성화에는 기존 [백엔드 실행 설정](../Backend/README.md)에 다음 값을 추가한다. 환경 변수는 `:` 대신 `__`를 사용한다. 비밀 값은 서비스 환경 또는 비밀 저장소로 주입하고 저장소에 기록하지 않는다.

| 설정 키 | 값/조건 |
|---|---|
| `Operations__Enabled` | `true` |
| `Operations__EnvironmentId` | 환경별 고유 값, 영문/숫자/`-`/`_`, 최대 64자 |
| `Operations__ManagementOrigin` | 실제 관리 HTTPS origin. 경로·query 없음. 예: `https://ops.example.invalid` |
| `Operations__AllowedNetworks__0` 등 | 운영자/VPN의 허용 CIDR. 기본 loopback만 허용 |
| `Operations__KnownProxies__0` 등 | forwarded header를 신뢰할 실제 proxy IP. 직접 HTTPS 접속이면 비움 |
| `Operations__ClientId`, `Operations__ClientSecret` | 운영 로그인 전용 GitHub OAuth 앱 |
| `Operations__AllowedGitHubUserId` | 허용된 개인 계정의 양의 숫자 ID. 사용자 이름 아님 |
| `Operations__AuthEpoch` | 16~128자 인증 세대 식별자. 변경 후 재시작하면 기존 세션 차단 |
| `Operations__KeyDirectory` | 지속되는 비공개 절대 경로. 재시작 시 같은 키 사용 |
| `Operations__LogRoot` | 키와 다른 비공개 절대 경로. 정적 웹/Content/Plugins와 분리 |
| `Operations__KeyCertificatePath`, `Operations__KeyCertificatePassword` | 키 암호화용 PFX. Linux 운영 환경 필수. HTTPS 서버 인증서와 별개 |
| `Operations__AllowUnencryptedDevelopmentKeys` | 기본 false. Development/Testing에서만 명시적으로 true 허용 |
| `Operations__SourceId` | 기본 `backend`; S1 단일 호스트 소스 식별자 |
| `Operations__Compensation__Enabled` | 기본 false. 개별 보상/API/조치 조회 활성화 |
| `Operations__Compensation__AllowedDefinitions__0` 등 | 기본 빈 목록으로 지급 불가. 지원하는 서버 정의/버전의 정확한 key만 허용 |
| `Operations__Monitor__Enabled` | 기본 false. 별도 기계 상태 조회 활성화 |
| `Operations__Monitor__Token` | 기계 인증 전용 32~256자 영문/숫자/`-`/`_` 비밀값. 게임 관리자/OAuth 자격과 분리 |

Windows에서 키 보호 인증서를 생략하면 서비스 계정의 DPAPI로 암호화한다. 해당 계정과 키 디렉터리를 함께 유지한다. Windows 디렉터리 ACL은 서비스 계정과 필요한 관리자만 접근하도록 배포 때 설정한다. Linux에서는 디렉터리 권한을 0700으로 설정한다. reparse point/junction 경로는 거절한다. 운영 호스트 시간대는 UTC가 필요하며 시작 시 검사한다.

운영 환경은 PostgreSQL을 사용한다. Memory는 Development/Testing 전용이고 재시작 때 운영 세션과 게임 상태가 유지되지 않는다. `Operations:ManagementOrigin` 설정 자체는 HTTPS listener나 VPN을 생성하지 않는다. 기존 Kestrel/역방향 proxy에 HTTPS와 관리망을 먼저 구성하고, 공개 게임 ingress에서는 `/ops` 접근을 차단한다. proxy가 있다면 실제 peer IP를 KnownProxies에 넣고 전달된 원래 운영자 IP를 AllowedNetworks로 검사한다. origin/관리망 검사가 실패하면 403이다.

GitHub 앱 callback은 정확히 `<ManagementOrigin>/ops/auth/github-callback`이다. 저장소/메일 scope를 요청하지 않으며 외부 access token은 서버 세션에 보존하지 않는다. 이 구현의 자동 검증은 외부 GitHub 응답만 모의 처리했다. 실제 OAuth 앱 등록, 관리 도메인/HTTPS 연결과 개인 계정 로그인 확인은 해당 배포 환경의 입력이 필요하다.

## 스키마와 시작 순서

1. 같은 릴리스의 Backend와 `ProjectRpg.Operations.Core.dll`, 컴파일된 Razor, `Database` SQL을 함께 publish한다. Backend의 ProjectReference가 Core를 포함한다. NuGet 전이 의존성은 [packages.lock.json](../Backend/ProjectRpg.Backend/packages.lock.json)으로 고정한다.
2. 기존 배포 절차대로 별도 DDL 역할에서 `dotnet ProjectRpg.Backend.dll --migrate-schema`를 실행한다. [0004_operations_sessions.sql](../Backend/Database/Migrations/0004_operations_sessions.sql)은 운영 세션, [0005_operations_evidence.sql](../Backend/Database/Migrations/0005_operations_evidence.sql)은 영수증/정산 조회 인덱스, [0006_operations_compensation.sql](../Backend/Database/Migrations/0006_operations_compensation.sql)은 확정 조치와 사건 키를 추가한다. 기존 v1~v5 SQL은 변경하지 않았다. 운영툴이 비활성이어도 새 릴리스의 migration catalog는 v6다.
3. 런타임 역할에 기존 게임 권한과 `ops_operator_sessions`의 SELECT/INSERT/UPDATE/DELETE, `ops_actions`와 `ops_compensation_cases`의 SELECT/INSERT 권한을 부여한다. 감사/사건 테이블의 UPDATE/DELETE는 런타임 역할에 부여하지 않는다. 스키마 초기화가 권한을 자동 부여하지는 않는다. 일반 운영 기동은 기존 계약대로 migration 이력을 검증한다.
4. 운영 설정을 주입하고 기존 백엔드를 기동한다. 관리망에서 `/ops/login` → GitHub 로그인 → `/ops` 검색을 확인한다. 조회는 게임 상태를 바꾸지 않지만 운영 세션의 last_seen/revoke/purge는 기록한다.

서버 세션의 원문 키는 DB에 저장하지 않는다. SHA-256 키 해시와 Data Protection으로 보호한 ticket만 저장한다. 로그아웃 때 서버 세션을 취소하며, 세션 저장소 장애를 인증 성공으로 처리하지 않는다. 키·세대 변경/복원 이후에는 기존 로그인 유지 여부를 별도로 확인한다.

## 사용 흐름

`/ops`에서 정확한 Steam ID 또는 캐릭터 GUID로 검색한 뒤 캐릭터 상세를 연다. 상세의 활성 재화·가방·장비·우편·던전/정산 패널은 선택한 패널만 조회한다. `ObservedExpired`는 저장된 리스의 만료 관측이며 현재 접속/종료를 확정하지 않는다. 성장 정보가 없거나 해석되지 않을 때는 Missing/Unavailable로 구분한다.

화면 하단의 추적 번호를 누르면 `/ops/diagnostics`에서 해당 요청 로그를 검색한다. API는 응답 `meta.httpRequestId`, `traceId`와 `X-Ops-Request-Id`를 제공한다. 로그 검색은 정확한 ID 또는 허용된 이벤트 필터가 필요하며 기본 최근 1시간, 최대 24시간이다. JSON API와 페이지/오류/필터 목록은 [구현 계약 §3](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md#3-s1-화면과-호출-계약)을 따른다.

커서는 환경·운영자·입력/정렬·페이지 크기에 묶인 보호 token이며 15분 뒤 만료한다. 이어보기 시 첫 검색의 기간을 유지한다. 커서 만료 또는 보존 파일 삭제 시 처음부터 다시 조회한다. 로그 검색은 최대 200건·3초·64MiB, 역방향 JSONL 읽기다. 손상/미완성/과대 행과 보존 유실은 Partial/Unknown으로 표시한다. 부분 결과나 0건은 게임 처리 성공/실패의 증거가 아니다.

기술 로그는 하루 또는 32MiB마다 회전하고 최대 30파일/14일을 보존한다. 4096개 비동기 큐가 넘치면 요청을 막지 않고 drop 수를 기록한다. 종료 flush 한도는 5초다. `catalogue.json`은 파일 관측/부팅/유실 정보를 담고, 실제 기록은 `ops-*.jsonl`이다. Windows에서 쓰는 중인 파일의 길이는 읽기 handle에서 확인한다. 기술 로그는 감사 원장이나 지급 영수증이 아니다. cookie/token/code/요청 body/SQL·예외 원문은 기록하지 않는다.

## 보상 근거 조사(S2)

S2에서는 캐릭터 상세의 **보상 근거 조사**로 이동한다. 기본 최근 7일/최대 30일이며 재화 영수증·아이템 영수증·정산을 시간 역순으로 합친다. 같은 시각에는 CurrencyReceipt → ItemReceipt → Settlement, 각 출처의 GUID 역순이다. 소스별 마지막으로 반환한 위치를 커서에 보존하며 패널/페이지 전체가 하나의 과거 DB snapshot이라는 보장은 없다.

`GET /ops/api/characters/{id}/timeline`은 이 목록을, `GET /ops/api/characters/{id}/evidence/{sourceKind}/{sourceKey}`는 대상과의 연결을 재검사한 단일 기록을 반환한다. 임의 SQL/원문 JSON은 노출하지 않는다. 재화 변화는 최대 16개, 아이템 ID는 최대 64개이며 생략 시 detailsTruncated를 표시한다. sourceKind는 CurrencyReceipt/ItemReceipt/Settlement다. 존재하더라도 해당 캐릭터와 연결되지 않으면 단일 조회는 404다.

`Delivered`는 해당 영수증의 처리가 확정됐다는 뜻이다. Consume/Move 등 후속 처리도 operation을 함께 읽는다. 정산 Pending/Processing은 Pending, Failed는 Failed, Completed만 있는 경우에는 개별 보상을 단정하지 않고 Unknown이다. 소비 이후에도 과거 지급 영수증은 Delivered로 남는다. 아이템은 실행자 또는 결과 소유자로만 연결되므로 소유 이력 범위는 Partial이며, 결과 소유자만 일치할 때 다른 참가자의 아이템 ID는 제외한다. 재화의 Account/Roster 잔액 변화는 영수증 내부의 실제 scope/ownerId로 표시한다.

원래 영수증에 sessionId가 없으면 생성해 채우지 않는다. 정산 시도 로그가 있으면 transactionRequestId로 원래 세션과 연결해 조사한다. 목록의 로그 링크는 사건 전후 5분을 사용한다. worker는 성공 commit 뒤 각 재화/아이템 요청 ID를 `game.reward.receipt.committed`에 남기며, 시작/완료/실패/재시도도 같은 시도 trace에 연결한다. 관측기 실패는 보상 commit/정산 처리를 변경하지 않는다. 과거/유실 로그는 Unknown이다. 개별 보상 활성 시 운영 조치는 캐릭터의 보상 화면에서 별도 조회하며 타임라인의 아이템 영수증과 원래 요청 번호로 연결한다.

일부 DB 출처만 실패하면 조회된 근거와 실패한 출처를 함께 보여 주고 이어보기는 중단한다. 전체 출처 실패는 503이다. 복구 후 처음부터 조회하며, 누락된 출처를 건너뛰는 커서는 발급하지 않는다. 기술 로그 서비스에 의존하지 않고 DB 근거를 조회한다.

## 개별 아이템 보상(S3 첫 범위)

캐릭터 상세 → **개별 보상·최근 확정 조치**에서 최근 보상과 조사 근거를 확인하고 사건 UUID, 품목, 10~512자 사유를 입력한다. 미리보기는 읽기만 수행한다. 확인 후 지급하면 우편 첨부 1개를 만들며 가방/재화는 직접 수정하지 않는다. 품목은 `RPGItemDefinition:<정확한 DefinitionName>:1` 형식이다. 예: `RPGItemDefinition:GameItem.Material.Progression.Ore:1`. 서버가 이미 지원하는 FirstSliceEquipmentDefinitions의 장비 7종(+0), 광석, 토큰만 등록할 수 있으며 기존 생성 규칙의 seed 0·기본 속성·CharacterBound를 사용한다. 임의 stat/seed/수량/정의 버전 입력은 받지 않는다. 기본 허용 목록은 비어 있고 실제 운영 품목 등록은 배포 설정이다.

[Core 보상 유스케이스](../Backend/ProjectRpg.Operations.Core/Compensation/Compensation.cs)는 권한·정규화·미리보기/정책 계약을, [저장 어댑터](../Backend/ProjectRpg.Backend/Operations/Infrastructure/Compensation/)는 원자 저장을 맡는다. 웹은 보호 token/CSRF/DTO를 다룬다. 5분 미리보기는 환경·운영자·정규화 입력·정책 버전에 묶인다. 새 요청만 만료/현재 정책을 검사하고 기존 성공은 원래 결과를 재생한다. 허용 목록 변경은 재시작 후 새 정책 버전이며, 목록을 비워도 이미 확정한 요청의 조회·재생은 가능하다. 전체 보상 단계 자체를 끄면 관련 경로도 닫힌다.

PostgreSQL에서 운영 요청 → 캐릭터 → 기존 아이템 owner/request → 리스 → 세션 부모 순서로 잠근다. 활성/경합 리스, 저장된 Waiting/Loading/InProgress/SettlementPending 참가 기록, Pending/Processing 정산이 있으면 `ops.target_busy`다. 만료 리스/세션 정리는 하지 않는다. FK 잠금으로 새 참가 행 삽입과 직렬화한다. 아이템·게임 영수증·`ops_actions`·사건 유일 키를 한 트랜잭션에 저장한다. 같은 사건/캐릭터의 새 요청은 409, 같은 요청/다른 입력도 409다. 영수증/감사 불일치는 500으로 멈춘다. Memory도 공유 dungeon/item/character 잠금 안에서 검사·효과·감사를 적용하고 실패하면 아이템 snapshot을 복원한다. Memory 재시작 내구성은 없다.

`POST /ops/api/compensations/preview`, `POST /ops/api/compensations`, `GET /ops/api/actions/{requestId}`, `GET /ops/api/actions?targetScope=Character&targetId=...`를 제공한다. POST는 CSRF 헤더 `X-Ops-CSRF`, 32KiB body, 운영자별 30회/분·10초 요청 제한이다. 최초 확정 201/재생 200, 최근 조치 50/최대 200건·15분 커서다. DB commit 응답이 불명확하면 503 `ops.outcome_unknown`이며 실패로 확정하지 않는다.

화면은 **POST 전에** 환경/운영자별 최근 요청 번호 최대 20개를 localStorage, 미리보기 token을 sessionStorage에 보존한다. 저장소를 쓸 수 없으면 지급하지 않는다. 응답 유실/새로고침 후 원래 번호 조회와 같은 ID 재시도를 제공하며 404를 지급 실패로 단정하지 않는다. 만료된 미리보기도 같은 사건·입력·요청 번호를 유지한 채 갱신한다. 로그아웃은 token만 지우고 요청 번호를 남긴다. 보호 키가 유실되면 token 재시도 대신 원래 요청 번호로 영수증을 조회한다. 자유 입력 사유는 감사에만 남고 기술 로그에는 넣지 않는다.

`ops.command.started/committed/replayed/rejected/unknown`은 HTTP trace와 운영 요청·아이템 거래·사건·아이템 ID를 연결한다. 감사 원장은 기술 로그 유실과 무관하게 DB에 보존된다. UI의 성공 표시는 지급 확정이며 UE의 즉시 갱신을 보장하지 않는다. 다음 로비·우편 재조회 반영이 계약이다. 기간 제재/해제는 이 범위에 포함하지 않았다.

## 런타임 감시·알림(S4 독립 범위)

외부 관리 호스트에서 [Operations.Monitor](../Backend/ProjectRpg.Operations.Monitor/)를 실행한다. 게임 backend와 다른 프로세스/호스트여야 backend 종료도 감지한다. [Core 감시 정책](../Backend/ProjectRpg.Operations.Core/Monitoring/MonitorCycle.cs)은 HTTP/파일 구현에 의존하지 않으며 probe·상태 저장·알림 전달 포트를 사용한다. 백엔드 [기계 상태 경계](../Backend/ProjectRpg.Backend/Operations/Monitoring/OperationsMonitoring.cs)는 기존 인증/입장 API와 독립적이다.

`GET /internal/operations/status`는 관리 HTTPS origin·허용 네트워크와 전용 bearer를 요구한다. 운영 cookie와 게임 관리자 bearer는 이 경로의 자격이 아니며 monitor token도 운영/게임 API 권한을 주지 않는다. 정확한 GET만 제공하고 query는 거절하며 분당 30회·5초 제한이다. forward header는 기존 KnownProxies에서 온 관리 요청에만 적용한다. 공개 ingress에서 `/internal/operations`도 차단해야 한다. 응답은 observedAtUtc/bootId/ready, 정산 Pending·Processing·Failed 수/가장 오래된 미완료 생성 시각/최근 Failed 갱신 시각, 로그 droppedCount/sinkErrorCount/retainedBytes/파일 관측 lastWriteAtUtc뿐이다. DB/로그 관측 실패는 Unknown/503으로 응답하며 식별자·오류 본문·SQL을 노출하지 않는다. PostgreSQL 집계는 READ ONLY/REPEATABLE READ로 게임 상태를 바꾸지 않는다.

감시기는 30초마다 `/health/live`, `/health/ready`, 기계 상태를 함께 수집한다. 한 회 수집 한도는 5초이며 HTTP 실패·과대/손상/오래된 응답을 최신 정상값으로 취급하지 않는다. 3회 연속 수집 실패에 장애 알림, 2회 연속 정상에 복구 알림, 지속 장애는 30분마다 요약한다. Failed 정산은 최초 관측과 수 증가/최근 실패 시각 전진 시 즉시 알린다. Pending/Processing이 2분 넘게 남은 상태는 3회 연속 관측 시 알린다. 로그 drop/sink error 증가와 설정 공간 예산 초과도 감지한다. 관측 실패는 정산·로그 장애를 해제하는 정상 표본으로 쓰지 않는다.

`dotnet publish Backend/ProjectRpg.Operations.Monitor/ProjectRpg.Operations.Monitor.csproj -c Release`로 감시기를 별도 배포하고 다음 환경 변수를 서비스에 주입한다. 실제 비밀값은 저장소에 넣지 않는다.

| 외부 감시 환경 변수 | 조건 |
|---|---|
| `RPG_MONITOR_ENVIRONMENT`, `RPG_MONITOR_SERVICE` | 각 영문/숫자/`-`/`_` 1~64자, 예: 환경 ID와 `backend` |
| `RPG_MONITOR_BACKEND_ORIGIN` | 관리 HTTPS origin, 경로/query 없음 |
| `RPG_MONITOR_TOKEN` | backend의 Operations Monitor Token과 같은 전용 기계 비밀값 |
| `RPG_MONITOR_WEBHOOK_URL` | 배포에서 지정한 HTTPS 수신 endpoint |
| `RPG_MONITOR_WEBHOOK_TOKEN` | 수신 endpoint 전용 bearer 비밀값 |
| `RPG_MONITOR_STATE_DIRECTORY` | 서비스 계정만 쓰는 비공개 절대 경로. Windows ACL은 배포에서 제한 |
| `RPG_MONITOR_LOG_BUDGET_BYTES` | 기본 1GiB, 최소 1MiB. 실제 보관 예산 |

실행은 `dotnet ProjectRpg.Operations.Monitor.dll`, 단일 수집은 `--once`다. TLS 인증서를 정상 검증하고 redirect는 따라가지 않는다. webhook URL/인증과 응답 본문/예외 원문은 출력하지 않는다. 감시 서비스의 프로세스 생존은 배포 호스트의 서비스 관리자에서 관리하고 종료 코드 1은 설정/상태/실행 오류로 조사한다.

상태 디렉터리의 `monitor.lock`을 단독 소유하고 `state.json`에 환경·서비스·incident 상태와 미전송 event를 보관한다. 전송 전에 eventId를 저장하고 파일 flush/원자 교체 후 HTTP를 보낸다. 성공하지 못하면 같은 eventId로 1/5/15분 간격 최대 3회 재전송하며 지속 장애 요약은 별도 event다. 수신자는 eventId로 중복을 제거해야 한다. 손상/다른 환경의 상태는 자동 초기화하지 않고 오류로 멈춘다. 상태 디렉터리를 삭제하면 알림 중복이 생길 수 있으므로 배포 때 유지한다. 링크/reparse 경로와 64KiB 초과 상태를 거절한다. 전체 원문 개인정보 대신 고정 summary와 빈 referenceIds를 보낸다.

이번 범위는 런타임/정산/로그 감시다. pgBackRest 상태 수집·백업 실패/보존 경보, Drain 장기 지속 경보는 각각 백업 및 서비스 제어 구현에 연결할 후속 범위다. 실제 외부 관리 호스트·알림 수신 서비스 배포와 장애 주입 리허설은 별도로 수행한다. 제재·해제는 공통 입장 API와 UE 검증 자원 인계 후 진행한다.

## 검사 실행

저장소 루트에서 실행한다. PostgreSQL 검사는 Docker와 이미 준비된 `postgres:17-alpine` 이미지가 필요하며 기존 DB에는 연결하지 않는다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsTests.ps1 -Postgres
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-NPCPickupBackendSnapshot.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-InventoryConsumeBackendSnapshot.ps1
```

운영툴 러너는 게시 HEAD의 Backend에 해당 운영툴 소스만 덮은 닫힌 입력을 만들고 별도 bin/obj로 빌드한다. 실제 loopback HTTPS, 실제 ASP.NET OAuth/cookie/antiforgery handler, 모의 GitHub backchannel을 사용한다. PostgreSQL은 실행별 라벨·동적 loopback 포트·tmpfs 컨테이너다. 후속 구현에서 입력 경로를 추가하면 러너의 소유 overlay 목록도 갱신한다.

검증 범위는 Core 의존 방향/권한·입력, 잘못된 OAuth state/비허용 계정/공급자 실패, CSRF·게임 인증 분리, int64 정밀도·페이지, 리스 조회 무변경, 로그 child span·커서·손상/회전, 유휴/절대 만료·로그아웃, PostgreSQL 세션/키 재시작 복원·AuthEpoch, v5→v6 단일 migration이다. S2는 실제 worker의 재화/아이템 원자 commit·관측기 예외 이후 처리, 동시각 출처 병합/페이지·actor/result 중복 제거·소비 후 영수증·소유 범위·정산 상태 구분·단일 조회의 연결 검증을 포함한다. 실제 게임 테이블 전체의 조회 전후 지문도 비교한다. 스냅샷 러너는 Core·Razor·SQL 의존성을 함께 포착하고 무관한 외부 참조는 계속 거절한다.

보상 검사는 실제 HTTPS/CSRF·기본 거절·정의 검증·body/rate 한도, 만료 뒤 동일 영수증·정책 변경/재시작, 동시 요청/사건 중복, PostgreSQL 감사 실패 후 전체 롤백·리스/세션 NOWAIT·부재 리스 FK 경합·독립 Pending/Processing 차단·영수증 불일치를 포함한다. 실제 페이지 스크립트를 DOM/storage/fetch 어댑터에서 실행해 응답 유실/새로고침/같은 ID 미리보기 갱신/저장소 실패/로그아웃도 검증한다.

감시는 정책의 실패/복구/지연/반복 경보와 영속 상태 재시작·중복 실행·재전송·손상 거절을 검사한다. 실제 HTTPS 상태/API 권한 분리·Memory/PG 읽기 전후 지문·민감 정보 제외·webhook 실패/재전송도 포함한다. 테스트는 자체 loopback 인증서를 신뢰하는 클라이언트를 사용하며 제품 감시기에는 인증서 검증 우회 설정이 없다.

자기 테스트 프로세스 종료 후 실행별 Temp와 자기 라벨 DB 컨테이너를 자동 폐기한다. 작은 결과/명령·입력 해시만 `Saved/BuildChecks`에 남으며 제품 커밋에는 포함하지 않는다. 이 검사는 실제 GitHub 계정 연결, 운영 배포, 실제 브라우저 렌더링, UE 실행 또는 부하 검사를 대신하지 않는다.
