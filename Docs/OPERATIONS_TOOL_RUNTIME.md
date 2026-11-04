# 운영툴 S1 실행과 유지보수

2026-11-05 · S1 구현: 개인 로그인, 캐릭터/아이템/재화/던전 조회, 운영 요청 로그 추적. [상세 계약](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md)의 S2 보상 조사·S3 지급/제재·S4 점검/감시/백업은 후속 구현 범위다.

## 구현 경계

- [Operations.Core](../Backend/ProjectRpg.Operations.Core/)는 업무 모델, 결과, capability, 읽기 포트와 유스케이스만 가진다. ASP.NET, Npgsql, Serilog, 게임 Backend 참조가 없다. 권한 확인 후 입력 검증과 포트 호출 순서로 처리한다.
- [Operations/Hosting](../Backend/ProjectRpg.Backend/Operations/Hosting/)은 DI·인증·관리망·요청 제한을 조립한다. [Web](../Backend/ProjectRpg.Backend/Operations/Web/)은 입력/커서/응답 변환을 담당하며 [Razor 화면](../Backend/ProjectRpg.Backend/Pages/Ops/)과 JSON API가 같은 유스케이스를 쓴다.
- [읽기 어댑터](../Backend/ProjectRpg.Backend/Operations/Infrastructure/Reads/)는 저장된 데이터만 관측한다. PostgreSQL은 READ ONLY / REPEATABLE READ, 명령 3초·요청 5초 제한이다. 조회가 만료 리스 정리나 성장 보상 반영을 호출하지 않는다. 패널 간 동일 snapshot은 보장하지 않는다.
- 운영 인증은 게임 Bearer와 별도다. GitHub 숫자 ID 한 개를 허용하고 PKCE/state·CSRF, Secure/HttpOnly cookie, 서버 세션을 사용한다. 세션은 유휴 30분/절대 8시간이며 로그아웃과 AuthEpoch 변경으로 무효화한다.
- 로그 수집/저장 구현은 Infrastructure에만 둔다. 요청 번호 → HTTP trace → PostgreSQL 읽기 child span을 연결한다. 전체 게임 서버·UE·정산 worker의 사건 계측과 보상 영수증 조사는 S2에서 확장한다.

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

Windows에서 키 보호 인증서를 생략하면 서비스 계정의 DPAPI로 암호화한다. 해당 계정과 키 디렉터리를 함께 유지한다. Windows 디렉터리 ACL은 서비스 계정과 필요한 관리자만 접근하도록 배포 때 설정한다. Linux에서는 디렉터리 권한을 0700으로 설정한다. reparse point/junction 경로는 거절한다. 운영 호스트 시간대는 UTC가 필요하며 시작 시 검사한다.

운영 환경은 PostgreSQL을 사용한다. Memory는 Development/Testing 전용이고 재시작 때 운영 세션과 게임 상태가 유지되지 않는다. `Operations:ManagementOrigin` 설정 자체는 HTTPS listener나 VPN을 생성하지 않는다. 기존 Kestrel/역방향 proxy에 HTTPS와 관리망을 먼저 구성하고, 공개 게임 ingress에서는 `/ops` 접근을 차단한다. proxy가 있다면 실제 peer IP를 KnownProxies에 넣고 전달된 원래 운영자 IP를 AllowedNetworks로 검사한다. origin/관리망 검사가 실패하면 403이다.

GitHub 앱 callback은 정확히 `<ManagementOrigin>/ops/auth/github-callback`이다. 저장소/메일 scope를 요청하지 않으며 외부 access token은 서버 세션에 보존하지 않는다. 이 구현의 자동 검증은 외부 GitHub 응답만 모의 처리했다. 실제 OAuth 앱 등록, 관리 도메인/HTTPS 연결과 개인 계정 로그인 확인은 해당 배포 환경의 입력이 필요하다.

## 스키마와 시작 순서

1. 같은 릴리스의 Backend와 `ProjectRpg.Operations.Core.dll`, 컴파일된 Razor, `Database` SQL을 함께 publish한다. Backend의 ProjectReference가 Core를 포함한다. NuGet 전이 의존성은 [packages.lock.json](../Backend/ProjectRpg.Backend/packages.lock.json)으로 고정한다.
2. 기존 배포 절차대로 별도 DDL 역할에서 `dotnet ProjectRpg.Backend.dll --migrate-schema`를 실행한다. 새 [0004_operations_sessions.sql](../Backend/Database/Migrations/0004_operations_sessions.sql)은 운영 세션 테이블과 조회 인덱스를 추가한다. v1~v3 SQL은 변경하지 않았다. 운영툴이 비활성이어도 새 릴리스의 migration catalog는 v4다.
3. 런타임 역할에 기존 게임 권한과 `ops_operator_sessions`의 SELECT/INSERT/UPDATE/DELETE 권한을 부여한다. 스키마 초기화가 권한을 자동 부여하지는 않는다. 일반 운영 기동은 기존 계약대로 migration 이력을 검증한다.
4. 운영 설정을 주입하고 기존 백엔드를 기동한다. 관리망에서 `/ops/login` → GitHub 로그인 → `/ops` 검색을 확인한다. 조회는 게임 상태를 바꾸지 않지만 운영 세션의 last_seen/revoke/purge는 기록한다.

서버 세션의 원문 키는 DB에 저장하지 않는다. SHA-256 키 해시와 Data Protection으로 보호한 ticket만 저장한다. 로그아웃 때 서버 세션을 취소하며, 세션 저장소 장애를 인증 성공으로 처리하지 않는다. 키·세대 변경/복원 이후에는 기존 로그인 유지 여부를 별도로 확인한다.

## 사용 흐름

`/ops`에서 정확한 Steam ID 또는 캐릭터 GUID로 검색한 뒤 캐릭터 상세를 연다. 상세의 활성 재화·가방·장비·우편·던전/정산 패널은 선택한 패널만 조회한다. `ObservedExpired`는 저장된 리스의 만료 관측이며 현재 접속/종료를 확정하지 않는다. 성장 정보가 없거나 해석되지 않을 때는 Missing/Unavailable로 구분한다.

화면 하단의 추적 번호를 누르면 `/ops/diagnostics`에서 해당 요청 로그를 검색한다. API는 응답 `meta.httpRequestId`, `traceId`와 `X-Ops-Request-Id`를 제공한다. 로그 검색은 정확한 ID 또는 허용된 이벤트 필터가 필요하며 기본 최근 1시간, 최대 24시간이다. JSON API와 페이지/오류/필터 목록은 [구현 계약 §3](OPERATIONS_TOOL_IMPLEMENTATION_SPEC.md#3-s1-화면과-호출-계약)을 따른다.

커서는 환경·운영자·입력/정렬·페이지 크기에 묶인 보호 token이며 15분 뒤 만료한다. 이어보기 시 첫 검색의 기간을 유지한다. 커서 만료 또는 보존 파일 삭제 시 처음부터 다시 조회한다. 로그 검색은 최대 200건·3초·64MiB, 역방향 JSONL 읽기다. 손상/미완성/과대 행과 보존 유실은 Partial/Unknown으로 표시한다. 부분 결과나 0건은 게임 처리 성공/실패의 증거가 아니다.

기술 로그는 하루 또는 32MiB마다 회전하고 최대 30파일/14일을 보존한다. 4096개 비동기 큐가 넘치면 요청을 막지 않고 drop 수를 기록한다. 종료 flush 한도는 5초다. `catalogue.json`은 파일 관측/부팅/유실 정보를 담고, 실제 기록은 `ops-*.jsonl`이다. Windows에서 쓰는 중인 파일의 길이는 읽기 handle에서 확인한다. 기술 로그는 감사 원장이나 지급 영수증이 아니다. cookie/token/code/요청 body/SQL·예외 원문은 기록하지 않는다.

## 유지할 검증

저장소 루트에서 실행한다. PostgreSQL 검사는 Docker와 이미 준비된 `postgres:17-alpine` 이미지가 필요하며 기존 DB에는 연결하지 않는다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-OperationsTests.ps1 -Postgres
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-NPCPickupBackendSnapshot.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Test-InventoryConsumeBackendSnapshot.ps1
```

운영툴 러너는 게시 HEAD의 Backend에 해당 운영툴 소스만 덮은 닫힌 입력을 만들고 별도 bin/obj로 빌드한다. 실제 loopback HTTPS, 실제 ASP.NET OAuth/cookie/antiforgery handler, 모의 GitHub backchannel을 사용한다. PostgreSQL은 실행별 라벨·동적 loopback 포트·tmpfs 컨테이너다. 후속 구현에서 입력 경로를 추가하면 러너의 소유 overlay 목록도 갱신한다.

검증 범위는 Core 의존 방향/권한·입력, 잘못된 OAuth state/비허용 계정/공급자 실패, CSRF·게임 인증 분리, int64 정밀도·페이지, 리스 조회 무변경, 로그 child span·커서·손상/회전, 유휴/절대 만료·로그아웃, PostgreSQL 세션/키 재시작 복원·AuthEpoch, v3→v4 단일 migration이다. 실제 게임 테이블 전체의 조회 전후 지문도 비교한다. 스냅샷 러너는 Core·Razor·SQL 의존성을 함께 포착하고 무관한 외부 참조는 계속 거절한다.

자기 테스트 프로세스 종료 후 실행별 Temp와 자기 라벨 DB 컨테이너를 자동 폐기한다. 작은 결과/명령·입력 해시만 `Saved/BuildChecks`에 남으며 제품 커밋에는 포함하지 않는다. 이 검사는 실제 GitHub 계정 연결, 운영 배포, 브라우저 시각 검사, UE 실행 또는 부하 검사를 대신하지 않는다.
