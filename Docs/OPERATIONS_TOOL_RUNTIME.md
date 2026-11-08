# 운영툴 실행과 유지보수

2026-11-08 · S1 개인 로그인/기본 조회/로그 추적, S2 보상 조사, S3 개별 보상·기간 제재/해제, S4 런타임 감시·알림과 공지·Open/Drain의 Backend/운영 화면, UE 로비 공지·점검 연결·장기 Drain 경보·백업 상태 수집/경보·격리 복원/PITR 리허설 도구, 선택형 게임 API HTTP 추적과 아이템·재화 거래 영수증 연결을 구현했다. 운영 용량/외부 환경 복구 리허설·실제 운영 배포는 후속 범위다. [상세 계약](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md).

## 공지·Open/Drain 서비스 제어

UE 로비의 공지/점검 연결과 캐시·실패·주소 전환·인증 보존 계약은 [온라인 로비 서비스 안내](ONLINE_LOBBY_UI.md#서비스-공지와-점검-안내)를 따른다. 신규 익명 조회 subsystem을 사용하며 운영자 자격을 게임 클라이언트에 전달하지 않는다.

`Operations:ServiceControlEnabled=true`와 기존 운영 인증/관리 HTTPS 설정을 적용하면 `/ops/service-control` 및 `GET/PUT /ops/api/service-control`을 제공한다. 기본 쓰기는 비활성이다. 배포 전 기존 스키마 이력을 유지한 채 `--migrate-schema`로 v9 `operations_service_control`을 적용한다. UI에서 모드·공지·시각·사유를 확인한 뒤 실행하며, 화면은 배포/프로세스 종료/DB 복원을 수행하지 않는다.

- 최초 상태는 Open/revision 0/빈 공지다. 변경은 기대 revision을 검사해 1 증가시키고 변경·원래 요청/운영자/HTTP/trace가 있는 불변 감사 영수증을 같은 트랜잭션으로 저장한다. 같은 ID/같은 정규 입력은 원래 영수증을 재생하고 다른 내용·다른 조치 종류의 ID 재사용은 거절한다. 조회는 상태를 변경하지 않는다.
- Core는 공지·시각·revision 정책과 좁은 읽기/원자 변경 포트를, 저장 어댑터는 영속화·트랜잭션을 소유한다. `PostgresAdmissionAccess`는 서비스→계정→게임 잠금 순서만 조립한다. 계정 제재 정책과 서비스 정책은 각각 독립돼 있다. 신규 입장은 서비스 공유 transaction advisory lock, 전환은 배타 lock을 사용한다. Memory는 기존 공통 transaction gate 안에서 같은 경계를 보장한다.
- Drain은 신규 로그인/인증 발급·던전 생성/참가·출발 요청·새 티켓 발급/소비·입장 확인을 503 `service_draining`/`Retry-After: 30`으로 거절한다. 출발 취소, 이미 인증된 조회/우편 수령, 기존 전투·heartbeat·정산과 읽기 전용 입장 근거는 유지한다. 기존 인증·멤버·리스·확정 보상을 삭제하지 않는다. Open 전환 후 신규 접근을 다시 허용한다.
- `GET /api/service-status`는 운영 화면 비활성 상태에서도 익명 읽기로 제공한다. 예외는 정확한 경로의 GET뿐이며 IP당 60회/분, no-store를 적용한다. 모드·plain text 공지·시작/종료·표시 여부·관측시각만 반환한다. 운영자·사유·운영 조치의 감사/요청/trace ID는 공개하지 않는다. 아래 선택형 게임 HTTP 추적을 켜면 이 조회 시도 자체의 번호는 공통 응답 헤더로 제공한다. 시작 포함/종료 제외의 UTC 구간이며 양쪽 시각을 생략하면 즉시·무기한, 빈 공지는 숨김이다. 클라이언트는 HTML로 실행하지 않고 텍스트로 표시해야 한다.
- UI는 int64 revision을 문자열로 유지하고 요청 ID와 원래 본문을 전송 전에 저장한다. 응답 유실/401/영수증 404는 미커밋으로 단정하지 않고 같은 요청의 조회/재시도만 제공한다. 영수증의 조치 종류·요청·내용 일치를 확인한 뒤 최신 상태를 다시 읽는다. `GET /ops/api/actions/{requestId}`와 화면 로그 링크로 원래 조치 및 `operationRequestId`를 추적한다. 기술 로그에는 공지/사유 본문을 넣지 않는다. PUT도 CSRF·32KiB·운영 명령 30회/분 제한을 적용한다.

필요한 검증은 기존 `Scripts/Tests/Run-OperationsTests.ps1 -Postgres`에 포함된다. 실제 HTTPS/모의 OAuth, Memory·격리 tmpfs PostgreSQL의 익명 경계/제한·권한/CSRF/본문·공지 시간·revision/재생·다른 종류 ID 충돌, 모든 신규 입장 거절과 기존 2인 heartbeat/실제 worker 양쪽 지급, 공유/배타 gate 대기·감사 실패 롤백·재시작 영속화·로그 검색을 검사한다. 별도 화면 스크립트 검사는 응답 유실/새로고침·동일 바이트 재전송·int64·401/404/영수증 불일치·저장 실패를 다룬다. 실제 GitHub OAuth·브라우저 시각 렌더·UE 화면/게임 소켓·운영 배포 검증과 구분한다.

Drain만으로 모든 쓰기가 멈췄다고 판단하면 안 된다. 배포/복구는 활성 던전·미확정/실패/처리 중 정산 확인 후 외부 수단으로 생산자·worker·백엔드를 중지하고 쓰기 차단을 확인해야 한다. 장기 Drain은 아래 외부 감시에서 경보하며 자동 강제 종료하지 않는다.

## 구현 경계

- [Operations.Core](../Backend/ProjectRpg.Operations.Core/)는 업무 모델, 결과, capability, 읽기/원자 지급 포트와 유스케이스만 가진다. ASP.NET, Npgsql, Serilog, 게임 Backend 참조가 없다. 권한 확인 후 입력 검증과 포트 호출 순서로 처리한다.
- [Operations/Hosting](../Backend/ProjectRpg.Backend/Operations/Hosting/)은 DI·인증·관리망·요청 제한을 조립한다. [Web](../Backend/ProjectRpg.Backend/Operations/Web/)은 입력/커서/응답 변환을 담당하며 [Razor 화면](../Backend/ProjectRpg.Backend/Pages/Ops/)과 JSON API가 같은 유스케이스를 쓴다.
- [읽기 어댑터](../Backend/ProjectRpg.Backend/Operations/Infrastructure/Reads/)는 저장된 데이터만 관측한다. PostgreSQL은 READ ONLY / REPEATABLE READ, 명령 3초·요청 5초 제한이다. 조회가 만료 리스 정리나 성장 보상 반영을 호출하지 않는다. 패널 간 동일 snapshot은 보장하지 않는다.
- 운영 인증은 게임 Bearer와 별도다. GitHub 숫자 ID 한 개를 허용하고 PKCE/state·CSRF, Secure/HttpOnly cookie, 서버 세션을 사용한다. 세션은 유휴 30분/절대 8시간이며 로그아웃과 AuthEpoch 변경으로 무효화한다.
- 로그 수집/저장 구현은 Infrastructure에만 둔다. 운영 조회는 요청 번호 → HTTP trace → PostgreSQL 읽기 child span을 연결한다. 게임 API는 아래 선택형 HTTP 관측 포트로 시도 번호·실제 Activity, 아이템·재화 거래 서비스의 확정/재생 영수증 번호를 연결한다. 정산 worker는 별도 관측 포트로 원래 세션/거래 요청 ID·시도 trace를 남긴다. 그 밖의 게임 업무 ID/저장 구간·UE 전체 사건 계측은 후속이며 같은 HTTP trace가 worker까지 전파됐다고 표시하지 않는다.

인터페이스를 추가할 때는 교체할 외부 의존성 또는 실제 유스케이스 경계가 있어야 한다. 새로운 지급 기능을 읽기 포트나 PageModel에 넣지 않는다. 여러 책임을 묶는 공통 repository/manager로 합치지 않는다.

## 게임 API 요청 추적

`Operations:Enabled=true`와 `Operations:GameApiDiagnosticsEnabled=true`를 함께 설정하면 `/api` 경계의 요청을 공통 계측한다. 기본값은 false이며 운영 저장소가 비활성인데 이 설정만 켜면 기동을 거절한다. `/ops`, `/internal/operations`, `/health`, `/apix`는 이 경계에 포함되지 않는다. 별도 migration은 없다.

- 요청마다 서버가 새 GUID를 만들고 응답 `X-Game-Request-Id`로 제공한다. 재시도와 동시 요청도 각각 새 번호를 갖는다. 클라이언트의 같은 이름 헤더나 본문의 업무 요청 ID를 내부 시도 번호로 사용하지 않는다. `X-Game-Trace-Id`는 실제 비어 있지 않은 W3C Activity가 있을 때만 제공하며 .NET이 수용한 `traceparent`의 trace를 공유할 수 있다. 이 번호들은 인증 자격이 아니다. 전송 중단·미처리 예외로 응답을 보내지 못하면 헤더 전달도 보장할 수 없다.
- `game.http.started`와 `game.http.completed`에 시도 번호·실제 trace/span·배포/인스턴스·고정 라우트 템플릿·상태·단조 시계 소요 시간을 남긴다. 실제 URL 경로 값, query, 요청/응답 본문, 인증/사용자 헤더, 예외 원문은 관측 포트에 전달하지 않는다. 라우트가 없거나 템플릿이 112자를 넘으면 `GET GameApi.Unmatched` 같은 고정 이름을 사용한다. 인증 전에도 실행하므로 401과 처리된 Drain 503이 관측된다.
- 정상 반환의 400 이상은 `Rejected`, 나머지는 `Succeeded`다. 밖으로 전파되는 예외는 `Faulted`이며 응답 시작 전에는 500, 이미 시작됐으면 상태를 null로 남긴다. 요청 취소는 `Canceled`/상태 null로 남겨 전송되지 않은 200/499를 합성하지 않는다. HTTP 성공은 게임 DB 반영 증거가 아니다.
- [HTTP 어댑터](../Backend/ProjectRpg.Backend/Operations/Hosting/GameHttpDiagnostics.cs)는 [Core의 IGameHttpObserver](../Backend/ProjectRpg.Operations.Core/Diagnostics/GameHttpObservation.cs)에 고정 모델만 전달한다. Infrastructure가 기존 유계 JSONL writer로 저장한다. 관측기 예외는 HTTP 상태·본문·업무 예외를 바꾸지 않는다. 기존 비동기 큐/보존 한도를 공유하므로 게임 요청량에 맞춘 실제 용량 검증은 배포 전에 수행한다.
- `/ops/diagnostics` 또는 `GET /ops/api/logs?httpRequestId=<GUID>`에서 같은 번호로 검색한다. `traceId`·두 이벤트 이름 필터도 기존 검색 권한/기간/예산을 적용한다. HTTP 이벤트 자체에는 주체·캐릭터·업무/거래 ID를 추정해 채우지 않는다. 아래 별도 영수증 이벤트에 확인된 거래 ID만 연결한다. 게임 저장 child span, UE→HTTP 문맥, 그 밖의 업무 영수증 연결은 후속이다. 검색 `Complete`는 선택 원본의 검색 완료이며 비활성 기간·과거·UE까지 수집됐다는 의미가 아니다.

`Scripts/Tests/Run-OperationsGameHttpTests.ps1 -Postgres`는 게시 Backend+본인 9개 C# 입력만 고정한 독립 빌드, 실제 loopback HTTPS/모의 OAuth, Memory·자기 tmpfs PostgreSQL에서 번호/실제 W3C 부모·재시도/동시성·권한 검색·라우트 마스킹·비밀 제외·게임 표 지문 불변을 검사한다. 예외/취소/관측기 실패는 경계 단위 검사다. `-FullSuite`는 운영툴 전체 Memory 회귀를 실행한다. 2026-11-08 두 모드 모두 빌드 0경고/0오류로 통과했다. finally로 사본·빌드 출력·런타임/DB를 폐기하며 작은 결과와 해시는 당시 실행 근거만 남긴다. 실제 UE 실행·운영 배포/부하 검증은 포함하지 않는다.

### 아이템·재화 영수증과 HTTP 연결

같은 `GameApiDiagnosticsEnabled` 설정으로 `POST /api/item-transactions/commit`과 `POST /api/economy/transactions/commit`의 확정 결과를 연결한다. [두 거래 서비스](../Backend/ProjectRpg.Backend/Application/)는 선택형 [IGameTransactionObserver](../Backend/ProjectRpg.Backend/Application/IGameTransactionObserver.cs)에 확인된 결과만 전달하며 ASP.NET·운영 로그 저장소를 참조하지 않는다. [Infrastructure 어댑터](../Backend/ProjectRpg.Backend/Operations/Infrastructure/Diagnostics/OperationsGameTransactionObserver.cs)가 현재 게임 HTTP 경계의 시도 번호와 실제 Activity를 붙인다. 기본값은 비활성이며 추가 migration은 없다.

- 저장소가 `Committed`를 반환하면 `game.transaction.receipt.committed`, 같은 명령의 확정 결과를 재생한 `AlreadyCommitted`이면 `game.transaction.receipt.replayed`를 한 건 남긴다. 재생 로그를 새 지급으로 계산하지 않는다. `transactionRequestId`는 반환된 영수증의 ID이며, 각 재시도의 `httpRequestId`는 별개다. 작업 이름은 `ItemTransaction.Commit`/`CurrencyTransaction.Commit`으로 고정한다.
- 재화의 `characterId`만 확정 결과에서 가져온다. 아이템의 actor/소유자 문자열을 캐릭터로 추정하지 않는다. 영수증에 원래 세션 정보가 없으므로 호출 본문의 `dungeonSessionId`를 재생된 영수증의 원래 세션처럼 기록하지 않는다. item/운영 요청/운영자 ID와 HTTP 상태도 이 이벤트에 합성하지 않는다. 여러 아이템은 거래당 한 이벤트로 묶으며 품목/수량/잔액·reason/fingerprint/임의 operation·자격/본문은 전달하지 않는다.
- 거절·충돌·권한 실패는 영수증 이벤트를 생성하지 않으며 공통 HTTP 경계에서 상태를 관측한다. 저장 예외 등으로 확정 결과를 받지 못한 경우에도 로그 부재가 미반영 증거는 아니다. 기존 DB 영수증을 원래 요청 번호로 재조회한다. 관측기 예외는 이미 확정된 결과·재생·기존 업무 예외를 변경하지 않는다. 추가 조회·트랜잭션·잠금은 만들지 않는다.
- `/ops/diagnostics`의 거래 요청 번호 또는 `GET /ops/api/logs?transactionRequestId=<GUID>`에서 확정/재생 → 해당 HTTP 시도 → trace를 조사한다. 번호를 알아도 기존 운영자 인증/진단 권한이 필요하다. HTTP 범위 밖의 직접 서비스 호출에는 가상의 HTTP 기록을 만들지 않는다. 원자 픽업은 아래 별도 경계에서 같은 관측 포트를 사용한다. worker·장비/우편/개인 거래 등 다른 직접 저장 경로는 이 두 일반 거래 서비스의 계측 범위에 포함되지 않는다. 기존 worker의 별도 관측 계약은 유지한다.

`Scripts/Tests/Run-OperationsGameTransactionTests.ps1 -Postgres`는 게시 Backend+본인 9개 C# 입력만 고정한 독립 Release, 실제 loopback HTTPS/모의 OAuth·Memory/자기 tmpfs PostgreSQL에서 실제 두 API의 확정/재생·충돌·미인증/관리자/멤버 권한 거절·영수증→HTTP/실제 trace 검색·게임 상태/불변 영수증 보존·비밀 제외를 검사한다. 별도 Memory HTTPS에서 관측기가 매번 예외를 던져도 두 거래의 효과가 한 번만 반영되고 재생/거절 응답이 유지되는지 확인한다. 저장 예외는 서비스 단위 검사다. `-FullSuite`는 운영툴 전체 Memory 회귀이며 2026-11-08 집중 PG와 전체 Memory 모두 0경고/0오류로 통과했다. 자기 빌드/실행 사본·DB는 finally 폐기한다. UE·실제 운영 배포/부하 검증과 구분한다.

### 던전 픽업 영수증 연결

`POST /api/dungeon-sessions/{sessionId}/pickup-intents/{requestId}/commit`의 [원자 픽업 서비스](../Backend/ProjectRpg.Backend/Application/DungeonPickupCommitService.cs)도 기존 `IGameTransactionObserver`를 재사용한다. 같은 `GameApiDiagnosticsEnabled` 설정으로 활성화하며 추가 API/자격/스키마는 없다. 저장소의 멤버·서버·세션/리스 검사와 예약 소비·지급 트랜잭션은 변경하지 않는다.

- 실제 `Committed`/`AlreadyCommitted` 결과와 단일 아이템 영수증이 일치할 때만 기존 `game.transaction.receipt.committed/replayed`를 기록한다. 고정 작업 이름은 `DungeonPickup.Commit`이다. 예약 준비/조회, 미존재·본문/권한/위치 등 거절, 저장 오류/취소를 확정 영수증처럼 기록하지 않는다. 로그 부재는 미반영 증거가 아니며 기존 원래 요청 ID로 결과를 복구한다.
- `transactionRequestId`, `characterId`, `itemId`는 동결 예약과 일치한 확정 영수증에서 가져온다. `dungeonSessionId`는 저장소가 이 원래 예약의 서버/세션 범위와 수령인 멤버십을 재검사하고 성공한 범위다. 미검증 route/body나 현재 이동한 아이템에서 연결 ID를 추정하지 않는다. 드롭 ID/동결 본문·fingerprint·정의/수량·계정·서버 자격은 기록하지 않는다.
- 이후 아이템 이동/소비가 있어도 재생은 원래 영수증과 연결하며 아이템의 현재 위치/개수를 덮어쓰지 않는다. 재시도마다 HTTP 시도 번호는 다르고 같은 거래 번호로 모아 조사한다. 관측기 예외는 성공/재생을 `InternalError`로 바꾸지 않도록 별도로 격리한다. 추가 저장 조회·잠금·트랜잭션은 만들지 않는다. HTTP 범위 밖의 직접 호출에는 HTTP 기록을 합성하지 않는다.
- 기존 8종 ID 검색/화면 링크로 거래→HTTP/trace와 캐릭터·아이템·던전 세션을 탐색한다. JSON schemaVersion 1과 사건 이름/기존 일반 거래의 필드는 유지하며, 일반 거래의 없는 세션/아이템 ID를 새로 채우지 않는다. UE 픽업 관측·실제 게임 소켓/Steam 연결이나 클라이언트 반영 완료를 뜻하지 않는다.

`Scripts/Tests/Run-OperationsPickupTracingTests.ps1 -Postgres`는 게시 Backend+본인 5개 C# 입력만 고정한 독립 Release에서 기존 일반 거래 추적 회귀와 실제 픽업 API의 HTTPS/모의 OAuth·Memory/자기 tmpfs PostgreSQL 검증을 실행한다. 백엔드 2인 멤버 세션의 실제 준비→원자 소비·지급→후속 아이템 이동→정확한 재생/동시 재시도, 원래 영수증/현재 아이템 보존, 올바른 수령인/세션/아이템/HTTP/실제 trace 검색과 컴파일된 Razor 링크를 확인한다. 별도 Memory HTTPS의 매번 실패하는 관측기에서도 단일 반영·성공/재생 응답을 확인하고 저장 오류/취소는 서비스 단위 검사한다. 재생/운영 조회 후 PG 게임 표 지문 불변·비밀 제외를 검사한다. `-FullSuite`는 전체 운영툴 Memory 회귀다. 실제 UE/브라우저 시각·운영 배포/부하는 미포함이며 자기 사본/출력/격리 DB는 finally 폐기한다.

2026-11-08 집중 Memory/PG와 전체 운영툴 Memory 회귀 모두 Release 0경고/0오류로 통과했다. 원자 저장소/API·DTO 변경 없이 기존 확정 거래 관측 포트를 확장한 범위다.

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
| `Operations__GameApiDiagnosticsEnabled` | 기본 false. `/api` 시도 번호·실제 Activity·안전한 시작/완료 이벤트. `Enabled=true` 필요 |
| `Operations__Compensation__Enabled` | 기본 false. 개별 보상/API/조치 조회 활성화 |
| `Operations__Compensation__AllowedDefinitions__0` 등 | 기본 빈 목록으로 지급 불가. 지원하는 서버 정의/버전의 정확한 key만 허용 |
| `Operations__Monitor__Enabled` | 기본 false. 별도 기계 상태 조회 활성화 |
| `Operations__Monitor__Token` | 기계 인증 전용 32~256자 영문/숫자/`-`/`_` 비밀값. 게임 관리자/OAuth 자격과 분리 |
| `Operations__RestrictionsEnabled` | 기본 false. 계정 제재/해제 API·화면 활성화. 기존에 저장된 제재의 게임 경로 검사는 이 설정과 무관하게 유지 |

Windows에서 키 보호 인증서를 생략하면 서비스 계정의 DPAPI로 암호화한다. 해당 계정과 키 디렉터리를 함께 유지한다. Windows 디렉터리 ACL은 서비스 계정과 필요한 관리자만 접근하도록 배포 때 설정한다. Linux에서는 디렉터리 권한을 0700으로 설정한다. reparse point/junction 경로는 거절한다. 운영 호스트 시간대는 UTC가 필요하며 시작 시 검사한다.

운영 환경은 PostgreSQL을 사용한다. Memory는 Development/Testing 전용이고 재시작 때 운영 세션과 게임 상태가 유지되지 않는다. `Operations:ManagementOrigin` 설정 자체는 HTTPS listener나 VPN을 생성하지 않는다. 기존 Kestrel/역방향 proxy에 HTTPS와 관리망을 먼저 구성하고, 공개 게임 ingress에서는 `/ops` 접근을 차단한다. proxy가 있다면 실제 peer IP를 KnownProxies에 넣고 전달된 원래 운영자 IP를 AllowedNetworks로 검사한다. origin/관리망 검사가 실패하면 403이다.

GitHub 앱 callback은 정확히 `<ManagementOrigin>/ops/auth/github-callback`이다. 저장소/메일 scope를 요청하지 않으며 외부 access token은 서버 세션에 보존하지 않는다. 이 구현의 자동 검증은 외부 GitHub 응답만 모의 처리했다. 실제 OAuth 앱 등록, 관리 도메인/HTTPS 연결과 개인 계정 로그인 확인은 해당 배포 환경의 입력이 필요하다.

## 스키마와 시작 순서

1. 같은 릴리스의 Backend와 `ProjectRpg.Operations.Core.dll`, 컴파일된 Razor, `Database` SQL을 함께 publish한다. Backend의 ProjectReference가 Core를 포함한다. NuGet 전이 의존성은 [packages.lock.json](../Backend/ProjectRpg.Backend/packages.lock.json)으로 고정한다.
2. 기존 배포 절차대로 별도 DDL 역할에서 `dotnet ProjectRpg.Backend.dll --migrate-schema`를 실행한다. v4는 운영 세션, v5는 조회 인덱스, v6는 확정 조치와 사건 키다. [0007_operations_restrictions.sql](../Backend/Database/Migrations/0007_operations_restrictions.sql)은 계정 제재와 계정 조치 대상을 추가한다. 기존 v1~v6 SQL은 변경하지 않았다. 운영툴이 비활성이어도 새 릴리스의 migration catalog는 v7이다.
3. 런타임 역할에 기존 게임 권한과 `ops_operator_sessions`의 SELECT/INSERT/UPDATE/DELETE, `ops_actions`와 `ops_compensation_cases`의 SELECT/INSERT, `ops_account_restrictions`의 SELECT/INSERT/UPDATE 권한을 부여한다. 제재 변경 화면을 꺼도 인증/입장 경로에는 `ops_account_restrictions` SELECT가 필요하다. 감사/사건 테이블의 UPDATE/DELETE는 런타임 역할에 부여하지 않는다. 스키마 초기화가 권한을 자동 부여하지는 않는다. 일반 운영 기동은 기존 계약대로 migration 이력을 검증한다.
4. 운영 설정을 주입하고 기존 백엔드를 기동한다. 관리망에서 `/ops/login` → GitHub 로그인 → `/ops` 검색을 확인한다. 조회는 게임 상태를 바꾸지 않지만 운영 세션의 last_seen/revoke/purge는 기록한다.

서버 세션의 원문 키는 DB에 저장하지 않는다. SHA-256 키 해시와 Data Protection으로 보호한 ticket만 저장한다. 로그아웃 때 서버 세션을 취소하며, 세션 저장소 장애를 인증 성공으로 처리하지 않는다. 키·세대 변경/복원 이후에는 기존 로그인 유지 여부를 별도로 확인한다.

## 사용 흐름

`/ops`에서 정확한 Steam ID 또는 캐릭터 GUID로 검색한 뒤 캐릭터 상세를 연다. 상세의 활성 재화·가방·장비·우편·던전/정산 패널은 선택한 패널만 조회한다. `ObservedExpired`는 저장된 리스의 만료 관측이며 현재 접속/종료를 확정하지 않는다. 성장 정보가 없거나 해석되지 않을 때는 Missing/Unavailable로 구분한다.

화면 하단의 추적 번호를 누르면 `/ops/diagnostics`에서 해당 요청 로그를 검색한다. API는 응답 `meta.httpRequestId`, `traceId`와 `X-Ops-Request-Id`를 제공한다. 로그 검색은 정확한 ID 또는 허용된 이벤트 필터가 필요하며 기본 최근 1시간, 최대 24시간이다. JSON API와 페이지/오류/필터 목록은 [구현 계약 §3](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md#3-s1-화면과-호출-계약)을 따른다.

### 로그 화면의 연결 탐색

`/ops/diagnostics`에서 HTTP 요청 번호, trace, 운영 요청, 조사 사건, 거래 요청, 캐릭터, 던전 세션, 아이템의 8종 ID를 입력한다. 여러 조건은 AND 검색이다. 이벤트 제안은 `OperationLogQuery.EventNames`의 등록 목록을 사용하므로 게임 HTTP·거래 영수증·worker 이벤트가 같은 화면에 나온다. 결과에는 수준/결과, 실제 값이 있는 HTTP 상태/소요 시간, 환경·서비스·인스턴스·빌드·span/부모 정보를 표시한다. HTML 본문과 폼 값은 Razor가 인코딩한다.

- 결과에 있는 유효한 ID를 누르면 해당 번호 하나로 새 검색을 한다. 실제 검색한 UTC 기간과 등록 원본을 유지하고 이전의 다른 ID·eventName·level·limit·cursor는 가져오지 않는다. 같은 거래의 다른 HTTP 시도나 같은 trace의 다른 사건이 이전 AND 조건에 가려지지 않게 한다. 기간을 입력하지 않았던 최초 검색도 당시의 기본 1시간을 고정한다.
- **계속 검색**은 같은 검색의 다음 페이지다. 원래 모든 조건·페이지 크기·고정 기간·보호 커서를 유지한다. 새로 추가된 로그를 기존 페이지 체인에 끼워 넣지 않는 EOF 계약도 유지한다. ID 링크로 새 검색할 때는 새 시점의 보존 원본을 읽는다.
- ID는 해당 이벤트에 저장된 값만 사용한다. null/빈 GUID/잘못된 trace에는 링크를 만들지 않고, 조회 원본이 없거나 여러 개여서 한 원본을 정할 수 없으면 추정하지 않는다. span/부모는 표시만 하며 지원하지 않는 검색 필터를 합성하지 않는다. 링크는 같은 관리 호스트의 `/ops/diagnostics`로만 향한다.
- [연결 링크 생성기](../Backend/ProjectRpg.Backend/Operations/Web/OperationsLogNavigation.cs)는 표시 필드와 새 검색 URL만 담당한다. [PageModel](../Backend/ProjectRpg.Backend/Pages/Ops/Diagnostics.cshtml.cs)은 기존 읽기 facade를 사용하고 [Razor](../Backend/ProjectRpg.Backend/Pages/Ops/Diagnostics.cshtml)는 표시만 한다. API/권한/검색 예산/보존 계약은 유지한다. 페이지는 미인증 시 기존 로그인으로 안내하고 JSON API는 401을 반환한다. 게임 관리자 Bearer로 운영 로그를 읽을 수 없다.

`Scripts/Tests/Run-OperationsLogNavigationTests.ps1 -Postgres`는 게시 Backend+본인 5개 C#/Razor 입력의 독립 Release, 실제 HTTPS/모의 OAuth·JSONL에서 8종 ID/현재 이벤트 제안, HTTP·trace 링크 실행, 기본 UTC 기간 고정/원본 유지·이전 AND 조건 제거, 이어보기/추가 행/커서 필터 불일치, 권한/경로 제한·저장/입력 HTML 인코딩·PG 게임 표 지문 불변을 확인한다. 풍부한 연결 관계는 합성 JSONL fixture이며, 별도로 실제 `/api/service-status`가 남긴 요청 번호→영속 HTTP 이벤트→실제 trace 링크를 확인한다. 합성 행을 실제 거래/게임 반영의 증거로 취급하지 않는다. `-FullSuite`는 운영툴 전체 Memory 회귀다. 2026-11-08 집중 PG와 전체 Memory 모두 Release 0경고/0오류로 통과했다. 실제 브라우저 시각 검증·UE 실행·운영 배포는 포함하지 않는다. 자기 사본/출력/격리 DB는 finally 폐기한다.

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

## 기간 제재·해제(S3 Backend/운영 화면)

캐릭터 상세의 **계정 기간 제재·해제**에서 현재 상태와 최근 조치를 확인한다. 제재는 기본 24시간/최대 30일이며 영구 제재는 제공하지 않는다. `None/revision=0`, `Active`, `Expired`, `Released`를 구분한다. 만료는 조회 시각으로 계산하고 상태/revision을 조회 과정에서 바꾸지 않는다. 해제도 별도 요청·사유·revision 증가·감사를 남긴다. 이미 만료/해제된 기록도 기대 revision이 맞으면 해제 영수증을 보존한다. 기록이 없는 계정의 해제는 404다.

[순수 Core 계약](../Backend/ProjectRpg.Operations.Core/Restrictions/AccountRestrictions.cs), [저장 어댑터](../Backend/ProjectRpg.Backend/Operations/Restrictions/), [계정 잠금](../Backend/ProjectRpg.Backend/Data/PostgresAccountAccess.cs)을 분리했다. 제재 성공은 `ops_actions`와 `ops_account_restrictions` 갱신·기존 `auth_sessions` 폐기가 같은 트랜잭션이다. 요청 ID → 계정 advisory gate → 계정/제재 행 순서로 잠근다. 로그인 계정 갱신/인증 발급, 방 생성/참가, 티켓 발급/소비/입장 확인도 같은 계정 gate를 게임 행보다 먼저 획득한다. 제재 이전에 발급한 미사용 티켓·소비 재시도도 이후 입장을 허용하지 않는다. 입장 근거 조회·기존 전투 heartbeat·정산은 보존한다. Memory도 같은 dungeon gate 아래 상태·감사·인증을 함께 반영한다.

새 제재의 만료/기대 revision 검사는 기존 성공 재생 뒤에 수행한다. 같은 요청/같은 정규화 입력은 만료·해제 후에도 원래 결과를 돌려준다. 다른 입력이나 다른 종류의 운영 조치에서 사용한 요청 번호는 409다. 제재와 로그인 경합은 새 인증을 거절하거나 먼저 발급된 인증을 제재 트랜잭션이 폐기한다. 해제·만료는 폐기한 옛 인증을 되살리지 않는다. 만료 이후 정상 재로그인한 새 인증은 해제 때문에 다시 폐기하지 않는다.

`POST /ops/api/account-restrictions`, `POST /ops/api/account-restrictions/{account}/release`, `GET /ops/api/account-restrictions/{account}`를 제공한다. 변경 요청은 공통 CSRF/32KiB/30회·분/10초 제한, int64 revision 문자열 계약을 따른다. 공통 `GET /ops/api/actions/{requestId}`는 보상/제재 영수증을 판별해 반환하고 `targetScope=Account&targetId=<SteamID>`로 계정 조치 이력을 조회한다. 계정 조치에는 `target_account_id`를 사용하고 캐릭터 보상의 `target_id`와 사건 키는 유지한다. 전체 이력의 일괄 편집/삭제 기능은 없다.

화면은 내용 확인 후 보내며, 전송 전에 환경/운영자별 최근 요청 최대 20개를 localStorage와 작업 intent를 sessionStorage에 저장한다. 응답 유실 후 원래 번호 조회/재전송을 제공한다. 저장소 실패는 전송을 거절한다. 로그아웃은 미전송 intent를 지우고 요청 번호는 보존한다. 과거 영수증을 복구해도 현재 상태는 다시 조회한다. revision 충돌 등 확정 거절 후에는 기록을 보존한 채 새로고침해 새 조치를 준비한다. 요청 결과가 404/불명확하다고 자동으로 새 번호를 만들지 않는다. 같은 `ops.command.*` 이벤트와 원래 요청/HTTP trace로 추적하며 사유는 감사에만 저장한다.

실제 HTTPS/Memory/PG에서 인증 폐기·로그인/모든 입장 경계 차단, 이미 입장한 2인 파티 heartbeat·실제 worker 지급 보존, 만료/해제·재시작·요청 충돌·감사 실패 롤백·로그인 경합을 검증했다. 실제 페이지 스크립트도 응답 유실/재시도/int64/저장소 실패/로그아웃을 검사했다. **최신 UE 인증·복구 소스는 다른 담당의 쓰기/검증 중이므로 수정하지 않았으며, 실제 최신 UE 실행에서 401 처리와 전투 보존을 확인하는 통합 검증은 남아 있다.** 이 Backend/운영 화면 완료를 S3 전체 UE 검증 완료로 표시하지 않는다.

## 런타임 감시·알림(S4 독립 범위)

외부 관리 호스트에서 [Operations.Monitor](../Backend/ProjectRpg.Operations.Monitor/)를 실행한다. 게임 backend와 다른 프로세스/호스트여야 backend 종료도 감지한다. [Core 감시 정책](../Backend/ProjectRpg.Operations.Core/Monitoring/MonitorCycle.cs)은 HTTP/파일 구현에 의존하지 않으며 probe·상태 저장·알림 전달 포트를 사용한다. 백엔드 [기계 상태 경계](../Backend/ProjectRpg.Backend/Operations/Monitoring/OperationsMonitoring.cs)는 기존 인증/입장 API와 독립적이다.

`GET /internal/operations/status`는 관리 HTTPS origin·허용 네트워크와 전용 bearer를 요구한다. 운영 cookie와 게임 관리자 bearer는 이 경로의 자격이 아니며 monitor token도 운영/게임 API 권한을 주지 않는다. 정확한 GET만 제공하고 query는 거절하며 분당 30회·5초 제한이다. forward header는 기존 KnownProxies에서 온 관리 요청에만 적용한다. 공개 ingress에서 `/internal/operations`도 차단해야 한다. 응답은 observedAtUtc/bootId/ready, 정산 Pending·Processing·Failed 수/가장 오래된 미완료 생성 시각/최근 Failed 갱신 시각, 로그 droppedCount/sinkErrorCount/retainedBytes/파일 관측 lastWriteAtUtc, `serviceControl:{mode:Open|Drain}`이다. 서비스 상태는 기존 읽기 포트를 사용하며 공지·revision·운영자·감사 정보를 포함하지 않는다. DB/로그/서비스 관측 실패는 Unknown/503으로 응답하며 식별자·오류 본문·SQL을 노출하지 않는다. PostgreSQL 정산 집계는 READ ONLY/REPEATABLE READ로 게임 상태를 바꾸지 않는다.

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
| `RPG_MONITOR_BACKUP_STATUS_PATH` | 백업 상태의 로컬 읽기 전용 절대 파일 경로. 빈 값은 백업 감시 비활성, 운영 배포에서 설정 |

실행은 `dotnet ProjectRpg.Operations.Monitor.dll`, 단일 수집은 `--once`다. TLS 인증서를 정상 검증하고 redirect는 따라가지 않는다. webhook URL/인증과 응답 본문/예외 원문은 출력하지 않는다. 감시 서비스의 프로세스 생존은 배포 호스트의 서비스 관리자에서 관리하고 종료 코드 1은 설정/상태/실행 오류로 조사한다.

상태 디렉터리의 `monitor.lock`을 단독 소유하고 `state.json`에 환경·서비스·incident 상태와 미전송 event를 보관한다. 전송 전에 eventId를 저장하고 파일 flush/원자 교체 후 HTTP를 보낸다. 성공하지 못하면 같은 eventId로 1/5/15분 간격 최대 3회 재전송하며 지속 장애 요약은 별도 event다. 수신자는 eventId로 중복을 제거해야 한다. 손상/다른 환경의 상태는 자동 초기화하지 않고 오류로 멈춘다. 상태 디렉터리를 삭제하면 알림 중복이 생길 수 있으므로 배포 때 유지한다. 링크/reparse 경로와 64KiB 초과 상태를 거절한다. 전체 원문 개인정보 대신 고정 summary와 빈 referenceIds를 보낸다.

### 장기 Drain 경보

`service_drain_long`은 정상 수집에서 처음 Drain을 관측한 시각부터 **30분 초과** 시 Warning/Firing을 발생시킨다. 30분마다 지속 요약을 보내고 Open을 두 번 연속 관측하면 Information/Resolved를 보낸다. `firstSeenAtUtc`는 임계값 도달 시각이 아니라 해당 관측 구간의 시작이다. 공지 편집·backend bootId 변경은 관측 구간을 초기화하지 않는다. 경보는 고정 summary와 빈 referenceIds만 사용하며 운영 사유·공지 원문을 전송하지 않는다.

정책은 실제 전환 시각 대신 **감시에서 관측한 기간**을 측정한다. 첫 설치/상태 유실 후 이미 Drain이면 그때부터 측정한다. 실패/오래된 상태/잘못되거나 누락된 mode, 2분 초과 관측 공백, 시계 역행은 미확정 대기 구간을 다시 시작한다. 이미 발생한 경보는 Unknown으로 해제하지 않으며 복구 연속 횟수도 초기화한다. 두 번의 Open 사이에 Drain이 관측되면 복구되지 않는다. 표본 사이의 미관측 전환까지 증명하는 기능은 아니다.

대기 구간과 경보·미전송 event는 기존 v1 `state.json`에 함께 영속한다. 감시 재시작은 대기 구간/같은 eventId 재전송을 보존하며, 재시작 공백이 2분을 넘으면 대기 구간을 다시 측정한다. 기존 v1 파일의 새 필드 부재는 빈 대기 구간으로 읽고 기존 장애/전송 대기는 보존한다. 배포는 mode를 제공하는 backend를 먼저, 새 monitor를 다음으로 갱신한다. 구 backend의 mode 누락을 Open으로 간주하지 않고 수집 실패로 경보한다. 새 사건 키를 모르는 구 monitor로 되돌릴 때 상태 파일을 임의 삭제하지 말고 보존/인계한다.

이번 감시 범위는 런타임/정산/로그/장기 Drain/백업이다. 실제 외부 관리 호스트·알림 수신 서비스 배포와 운영 복구 리허설은 별도로 수행한다. 제재의 실제 최신 UE 통합은 해당 소스/검증 자원 인계 후 진행한다.

## 백업 상태 수집·경보

[백업 운영 계약](OPERATIONS_BACKUP.md)에 Linux pgBackRest 작업 래퍼·60초 상태 수집·읽기 전용 관리 호스트 전달·환경 변수/권한·5개 사건 키를 정리했다. 앱/운영 웹에서 DB 복원을 실행하지 않는다. backend 종료와 백업 관측은 독립적으로 평가하며 파일 없음/손상/수집 실패/2분 갱신 중단을 Unknown으로 경보한다.

full8일/daily26시간 지연, 작업/저장소 실패, full2체인 미달, WAL 실패/2분 적체를 감시한다. idle DB는 archive 시각만 오래됐다고 실패로 판단하지 않는다. 사건과 재전송은 기존 상태/outbox를 사용한다. 실제 full2/incr1·암호화 repo·읽기 전용 SQL·WAL 실패 주입은 격리 DB에서 검증하며 원격 운영 repo·전달 수단·복원/PITR와 구분한다.

## 복원·PITR 리허설

[복구 리허설/운영 전환 계약](OPERATIONS_RECOVERY.md)에 읽기 전용 체크포인트·행 스트리밍 지문·별도 pause 복원본·소유 PID 정리와 운영 세션/AuthEpoch/자격 폐기 순서를 정리했다. full/incremental consistency 복원과 시점 복구, 데이터/키/WAL/목표 실패를 실제 격리 PostgreSQL에서 검사한다. 도구는 운영 DB를 덮어쓰거나 승격/연결 전환하지 않으며 운영 크기 RPO/RTO 달성을 주장하지 않는다.

## 검사 실행

저장소 루트에서 실행한다. PostgreSQL 검사는 Docker와 이미 준비된 `postgres:17-alpine` 이미지가 필요하며 기존 DB에는 연결하지 않는다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsTests.ps1 -Postgres
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsDrainTests.ps1 -Postgres
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsBackupTests.ps1 -FullSuite
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsRecoveryTests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-NPCPickupBackendSnapshot.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-InventoryConsumeBackendSnapshot.ps1
```

운영툴 러너는 게시 HEAD의 Backend에 해당 운영툴 소스만 덮은 닫힌 입력을 만들고 별도 bin/obj로 빌드한다. 실제 loopback HTTPS, 실제 ASP.NET OAuth/cookie/antiforgery handler, 모의 GitHub backchannel을 사용한다. PostgreSQL은 실행별 라벨·동적 loopback 포트·tmpfs 컨테이너다. 후속 구현에서 입력 경로를 추가하면 러너의 소유 overlay 목록도 갱신한다.

검증 범위는 Core 의존 방향/권한·입력, 잘못된 OAuth state/비허용 계정/공급자 실패, CSRF·게임 인증 분리, int64 정밀도·페이지, 리스 조회 무변경, 로그 child span·커서·손상/회전, 유휴/절대 만료·로그아웃, PostgreSQL 세션/키 재시작 복원·AuthEpoch, v6→v7 단일 migration이다. S2는 실제 worker의 재화/아이템 원자 commit·관측기 예외 이후 처리, 동시각 출처 병합/페이지·actor/result 중복 제거·소비 후 영수증·소유 범위·정산 상태 구분·단일 조회의 연결 검증을 포함한다. 실제 게임 테이블 전체의 조회 전후 지문도 비교한다. 스냅샷 러너는 Core·Razor·SQL 의존성을 함께 포착하고 무관한 외부 참조는 계속 거절한다.

보상 검사는 실제 HTTPS/CSRF·기본 거절·정의 검증·body/rate 한도, 만료 뒤 동일 영수증·정책 변경/재시작, 동시 요청/사건 중복, PostgreSQL 감사 실패 후 전체 롤백·리스/세션 NOWAIT·부재 리스 FK 경합·독립 Pending/Processing 차단·영수증 불일치를 포함한다. 실제 페이지 스크립트를 DOM/storage/fetch 어댑터에서 실행해 응답 유실/새로고침/같은 ID 미리보기 갱신/저장소 실패/로그아웃도 검증한다.

감시는 정책의 실패/복구/지연/반복 경보와 영속 상태 재시작·중복 실행·재전송·손상 거절을 검사한다. 실제 HTTPS 상태/API 권한 분리·Memory/PG 읽기 전후 지문·민감 정보 제외·webhook 실패/재전송도 포함한다. 테스트는 자체 loopback 인증서를 신뢰하는 클라이언트를 사용하며 제품 감시기에는 인증서 검증 우회 설정이 없다.

장기 Drain 전용 러너는 게시본에 명시한 8개 C# 파일만 덮고 `--monitoring` 검사를 실행한다. `-FullSuite`를 추가하면 같은 고정 입력에서 전체 운영툴 C# 회귀를 실행한다. 기존 감시 회귀와 30분 경계/관측 공백/Unknown/Open 복구·공지 편집·재시작 전후 대기/동일 사건 재전송을 검사한다. 실제 Memory/PG 저장소 모드와 loopback HTTPS를 사용하며 긴 경과 시간은 테스트 시계로 진행한다. 실제 30분 대기·외부 알림 서비스 배포 검증과 구분한다.

자기 테스트 프로세스 종료 후 실행별 Temp와 자기 라벨 DB 컨테이너를 자동 폐기한다. 작은 결과/명령·입력 해시만 `Saved/BuildChecks`에 남으며 제품 커밋에는 포함하지 않는다. 이 검사는 실제 GitHub 계정 연결, 운영 배포, 실제 브라우저 렌더링, UE 실행 또는 부하 검사를 대신하지 않는다.
