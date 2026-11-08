# 운영툴 구현 계약 v1 — 상세 설계 완료 기준

작성일: 2026-11-05 · 상태: v1 설계 확정, S1·S2 구현/격리 검증 완료. 실제 운영 배포 미수행

S1·S2·S3 보상/제재 Backend·S4 런타임 감시·공지/점검·장기 Drain 경보·백업 수집/경보·격리 복구 리허설의 소스 경계·설정·기동/검증 방법은 [운영툴 실행 문서](OPERATIONS_TOOL_RUNTIME.md)에 기록한다. 게임 API의 서버 생성 시도 번호·실제 Activity·안전한 시작/완료 이벤트는 [선택형 HTTP 경계](OPERATIONS_TOOL_RUNTIME.md#게임-api-요청-추적), 아이템·재화 거래 서비스의 확정/재생 영수증 연결은 [거래 추적 경계](OPERATIONS_TOOL_RUNTIME.md#아이템재화-영수증과-http-연결)로 구현했다. 그 밖의 게임 업무 ID/저장 구간·UE 전체 사건 연결, S3 제재의 실제 최신 UE 통합 검증과 S4 운영 용량/외부 환경 복구 리허설·배포는 후속 범위이며 현재 완료를 v1 전체 구현 완료로 취급하지 않는다.

사용자 요청은 SOLID·유지보수성·로그 추적을 갖춘 운영툴의 설계를 끝내는 것이다. 이 문서는 [아키텍처](OPERATIONS_TOOL_ARCHITECTURE.md)의 상세 계약을 확정한다. [제작 계획](SOLO_OPERATIONS_TOOL_PLAN.md)은 범위/순서, [추적 계약](OPERATIONS_LOG_TRACING.md)은 공통 사건 의미를 관리한다. 값·DTO·오류·동시성 정책의 최종 기준은 본 문서이며, 변경할 때 관련 요약도 함께 갱신한다.

## 1. 설계 완료의 범위

v1은 S1 운영자 로그인·검색/상세·로그 조회, S2 보상 조사, S3 개별 아이템 보상·기간 제재/해제, S4 공지·신규 입장 차단·알림·백업/복구 절차까지다. 전체 보상·예약 실행·자유 데이터 편집·재화 회수·직원별 역할 편집·경제 분석은 v1에서 제외한다. 이들 S5 기능의 미설계를 v1 미완료와 혼동하지 않는다.

| 결정 ID | 확정안 | 채택 이유 |
|---|---|---|
| D01 | GitHub OAuth 로그인 + 숫자 계정 ID 한 개의 허용 목록 | 직접 비밀번호/회원 관리 없이 개인 신원 구분. 게임 Steam 인증과 분리 |
| D02 | PostgreSQL 운영 세션 + 이름 있는 ASP.NET cookie scheme | 만료·로그아웃·신원 제거를 서버에서 집행 |
| D03 | 기존 호스트 + Razor Pages + 작은 Operations.Core | 배포 단위를 늘리지 않고 업무/웹/저장 책임 분리 |
| D04 | S1의 명시적 읽기 DTO·별도 읽기 어댑터 | 게임 조회의 만료 정리·성장 보상 반영 부작용 차단 |
| D05 | Serilog JSON 파일 + 제한된 로컬 검색 어댑터 | 별도 로그 서버 없이 영속 추적 시작, 공급자 변경은 어댑터에 한정 |
| D06 | v1 보상은 허용된 아이템 1종·1개의 개별 우편 지급 | 임의 능력치/재화 편집과 접속 중 가방 덮어쓰기 방지 |
| D07 | 기간 제재는 새 접근 차단, 현재 전투 강제 종료 없음 | 남은 파티·확정된 정산 보존 |
| D08 | 점검은 Open/Drain, 배포·DB 복원은 별도 실행 절차 | 운영 화면이 원격 프로세스/DB를 임의 종료하지 않음 |
| D09 | 외부 health 감시 + HTTP webhook, pgBackRest 백업 | 서비스가 죽어도 감지하고 검증된 백업 도구 활용 |

실제 OAuth 앱 ID/비밀키·운영자 숫자 ID·도메인·디렉터리·백업 저장소 주소는 **배포 입력**이다. 기능 동작을 정하지 못한 설계 항목이 아니다. 이 문서는 실제 계정 연결·서비스 구매·운영 설정 변경을 수행하지 않는다.

## 2. S1 인증과 접근

### 2.1 로그인 흐름

GitHub OAuth 앱을 운영 로그인 전용으로 등록한다. ASP.NET의 OAuth 핸들러 `OperationsGitHub`와 cookie scheme `OperationsCookie`, 정책 `OperationsAccess`를 사용한다. 기본 scheme을 바꾸어 기존 게임 Bearer를 대체하지 않는다.

1. `GET /ops/login`은 로그인 폼과 위조 방지 토큰만 표시한다. 이미 유효한 운영 세션이면 검색 화면으로 이동한다.
2. `POST /ops/auth/login`은 위조 방지 토큰·허용된 return path를 검사한 뒤 challenge한다. authorization code + PKCE S256, state/correlation 검증을 사용한다. 원격 인증 시도 수명은 10분이다.
3. callback은 `GET /ops/auth/github-callback`. GitHub의 고정 HTTPS token endpoint에서 code를 교환하고 `GET https://api.github.com/user`의 숫자 `id`를 확인한다. 요청으로 받은 사용자 이름·메일·GitHub 조직/저장소 소유권을 권한 근거로 쓰지 않는다.
4. `AllowedGitHubUserId`와 정확히 일치하면 새 운영 세션을 발급한다. 일치하지 않으면 403, 공급자 장애/시간 초과는 503이며 세션을 만들지 않는다. callback/state 재사용은 거절한다.
5. token·code·PKCE verifier는 로그/브라우저/영속 ticket에 저장하지 않는다. 외부 token은 신원 확인 후 폐기하고 `SaveTokens=false`로 둔다. 저장소/메일/쓰기 scope를 요청하지 않으며 전용 앱의 빈 scope 로그인만 사용한다.

GitHub는 authorization code/PKCE를 지원하고, scope 없는 인증 token으로도 공개 사용자 응답을 얻을 수 있다. 별도 권한을 가진 앱의 token을 재사용하지 않는다. OAuth 방식과 PKCE는 [GitHub 인증 문서](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps), 계정 ID 확인은 [사용자 API](https://docs.github.com/en/rest/users/users#get-the-authenticated-user)를 따른다. GitHub 계정의 2단계 인증 사용은 계정 관리 조건이며, scope 없는 응답으로 MFA 완료 여부를 검증했다고 표시하지 않는다.

### 2.2 세션 계약

| 항목 | 값/동작 |
|---|---|
| OperatorId | `github:<numeric-id>`; 값은 서버가 검증한 응답에서만 생성 |
| cookie | `__Host-ProjectRpgOps`, Secure, HttpOnly, Path `/`, Domain 미지정, SameSite=Lax, 브라우저 종료 후 영구 유지하지 않음 |
| 유휴/절대 만료 | 마지막 인증 요청부터 30분, 최초 로그인부터 최대 8시간. 갱신이 절대 만료를 연장하지 않음 |
| 서버 보관 | ASP.NET `ITicketStore`의 PG 구현. cookie에는 보호된 세션 참조만, DB에는 보호된 ticket과 세션 메타데이터 |
| 매 요청 확인 | 세션 존재·폐기/만료·허용 ID·설정의 AuthEpoch 일치. DB 확인 실패는 접근 거절/503이며 stale 권한 캐시 없음 |
| 갱신 | 인증 요청마다 만료/폐기 조건부로 last_seen 갱신. ticket 만료는 최초 로그인+8시간으로 고정하고 cookie SlidingExpiration=false; 30분 idle은 서버에서 집행. 운영 세션/로그 갱신은 게임 상태 변경이 아님 |
| 로그아웃 | `POST /ops/auth/logout`, CSRF 필수. 먼저 세션 폐기 확정 후 cookie 삭제. DB 장애면 성공했다고 표시하지 않음 |
| 설정 변경/복구 | 허용 ID 변경 또는 AuthEpoch 증가 시 기존 세션 무효. DB 복원 뒤 AuthEpoch 교체와 운영 세션 폐기를 먼저 수행 |
| 공급자 장애 | 이미 유효한 로컬 운영 세션은 만료까지 사용 가능. 새 로그인은 실패하며 공용 AdminToken 로그인 fallback 없음 |

cookie 보호와 로그아웃은 ASP.NET 인증 기능을 사용한다. 유휴/절대 만료·서버 폐기 규칙은 운영 세션 어댑터가 추가한다. [ASP.NET cookie 인증](https://learn.microsoft.com/en-us/aspnet/core/security/authentication/cookie?view=aspnetcore-9.0)

Data Protection 키는 배포마다 사라지는 임시 폴더에 두지 않는다. 별도 보호 경로와 환경별 ApplicationName을 사용한다. 다른 환경의 cookie·cursor를 수용하지 않는다. 키 파일 권한/암호화와 실제 경로는 배포 설정 검사 대상이다.

### 2.3 공개 경계와 요청 제한

- 기본 접속은 HTTPS 루프백 또는 사설 관리 네트워크다. 원격은 관리용 ingress만 `/ops`를 전달한다. 게임 공개 ingress는 `/ops`를 차단한다. 호스트에서도 정해진 관리 origin/접근 경로를 검사하며 forwarded header는 등록한 프록시에서만 신뢰한다.
- cookie가 있어도 `/api`에 필요한 게임 Bearer를 대신하지 못한다. 게임 Player/GameServer/Administrator token은 `/ops` 접근에 사용할 수 없다.
- 익명 허용은 로그인 화면·로그인 시작·정확한 callback 경로뿐이다. 정적 CSS에는 데이터/자격을 넣지 않는다. 로그/사용자 데이터 경로는 모두 정책 필수다.
- 운영자가 1명이므로 capability 집합은 고정한다: S1 `ReadCharacter`, `ReadDiagnostics`; S2 `ReadEvidence`; S3 `Compensate`, `RestrictAccount`; S4 `ManageService`. 단계 비활성 시 해당 경로를 등록하지 않는다.
- ingress 기준 로그인 시작은 IP당 10회/10분, callback은 IP당 30회/10분. 인증 조회는 운영자당 120회/분, 로그 검색은 별도 10회/분, 변경은 10회/분. 큐 대기 없이 429와 Retry-After를 반환한다. 초기 호스트 1개 기준이며 다중 호스트 전환 시 공유 제한 저장소를 같은 정책 어댑터에 추가한다.
- 처리 순서는 추적 번호 생성 → Ops 경로/ingress 제한 → 명시적 scheme 인증/인가 → 주체 제한 → 입력 검증 → 유스케이스다. 익명 요청에는 대상 존재나 필드 상세 오류를 먼저 노출하지 않는다.
- 변경 폼·JSON POST/PUT/DELETE·로그아웃은 CSRF 검사를 한다. callback은 OAuth state/correlation 검사로 보호한다. `returnUrl`은 `/ops` 내부 경로만 허용하며 외부 URL/스킴 상대 URL은 거절한다.

## 3. S1 화면과 호출 계약

Razor PageModel은 Core 유스케이스를 직접 호출한다. 아래 읽기 API도 S1에 함께 제공하되 업무 로직을 복제하지 않는다. 게임 로직을 포함하지 않는 Web 결과 매퍼만 공유한다.

| 페이지/API | 입력 | 유스케이스와 출력 |
|---|---|---|
| `GET /ops` | 없음 | 검색 폼·환경/빌드 표시 |
| `GET /ops/characters/{id}` | 캐릭터 GUID | 기본 상세 + 독립 패널 링크/조회 |
| `GET /ops/diagnostics` | 아래 로그 필터 | 8종 ID/등록 이벤트 검색·ID 연결 탐색·기존 조건의 이어보기 |
| `GET /ops/api/characters` | `kind=SteamId\|CharacterId`, `value`, `limit`, `cursor?` | `SearchCharacters` → Page<CharacterHeader> |
| `GET /ops/api/characters/{id}` | GUID | `GetCharacterSummary` → CharacterHeader |
| `GET /ops/api/characters/{id}/inventory` | `container=Inventory\|Equipment\|Mail`, `limit`, `cursor?` | `GetInventoryPage` → Page<ItemSummary> |
| `GET /ops/api/characters/{id}/wallet` | GUID | `GetWallet` → WalletSnapshot |
| `GET /ops/api/characters/{id}/sessions` | GUID, limit(기본 10/최대 50), cursor? | `GetSessionEvidence` → Page<SessionEvidence> |
| `GET /ops/api/logs` | `fromUtc`, `toUtc`, `sourceId?`, 정확한 ID/이벤트 필터, `limit`, `cursor?` | `SearchOperationLogs` → OperationLogPage |

첫 버전에서 이름 부분 검색·현재 값 수정 폼·자동 polling은 제공하지 않는다. 결과에 안정 ID를 표시하고 사용자 선택을 거쳐 상세로 이동한다. 기본 상세 실패는 페이지 오류, 개별 패널 실패는 해당 패널 오류로 표시한다. 다른 패널을 빈 성공으로 덮거나 화면 전체를 자동 재시도하지 않는다.

로그 화면의 [연결 탐색 구현](OPERATIONS_TOOL_RUNTIME.md#로그-화면의-연결-탐색)은 Web의 표시/URL 생성기와 PageModel·Razor를 분리한다. 지원하는 모든 ID 필드와 등록 이벤트 목록을 사용하고 실제 검색 UTC 기간/등록 원본을 고정한다. ID를 누를 때는 해당 ID만으로 새 검색해 이전 AND 조건·커서를 제거한다. 계속 검색은 같은 조건/페이지 크기·보호 커서를 유지한다. 실제 저장 값과 기존 권한/검색 계약만 사용하며 없는 연결·새 검색 권한을 만들지 않는다.

### 3.1 공통 형식과 검증

- JSON은 camelCase, `contractVersion=1`; UUID는 소문자 D 형식, 빈 UUID 거절. Steam ID는 1~20자리의 양의 uint64 십진 문자열이며 공백/부호/선행 0을 거절한다. 일반 검색값은 최대 128자이며 cursor·사유·공지에는 각 별도 한도를 적용한다. 알 수 없는 query/JSON 필드는 400, 명령 body는 최대 32KiB다.
- 모든 int64(재화, Revision, 성장 XP)는 JSON에서 십진 문자열이다. int32 수량/레벨/페이지 크기는 숫자다. 날짜는 UTC ISO-8601 `Z` 형식, null 허용 여부는 아래 모델을 따른다.
- 조회는 기본 50/최대 200건이며 범위 밖 limit은 400이다. 전체 목록을 얻으려고 저장소 limit을 해제하지 않는다. DB 명령 기본 timeout 3초, 전체 조회 deadline 5초, 클라이언트 취소 전달.
- 응답은 `Cache-Control: no-store`; API 인증 실패는 JSON 401/403이며 로그인 HTML로 리디렉션하지 않는다. 이름·로그 메시지는 HTML escape하며 API의 DB/예외 원문은 반환하지 않는다.
- 성공은 `{ data, meta }`. `meta={contractVersion:1, environment, observedAtUtc, httpRequestId, traceId:null|string}`. `Page<T>={items:T[], hasMore:boolean, nextCursor:null|string}`. 조회 0건은 성공한 빈 배열이다.
- 각 패널은 자기 observedAtUtc를 갖는다. 전체 상세 화면이 동일 DB snapshot이라는 보장은 하지 않는다.

### 3.2 정확한 조회 모델

| 모델 | 필드와 의미 |
|---|---|
| CharacterHeader | characterId:UUID, accountId:string(Steam ID, 권한 있는 화면에서만), rosterId:UUID, name:string, createdAtUtc:UTC, progression:{state:Available/Missing/Unavailable, classId:string/null, level:int/null} |
| ItemSummary | itemId:UUID, definitionType:string, definitionName:string, definitionVersion:int, quantity:int, revision:int64-string, containerType:enum, containerId:string, slotIndex:int, lifecycleState:enum, bindState:enum, isLocked:boolean, expiresAtUtc:UTC/null, durability:{current:int,maximum:int} |
| WalletSnapshot | characterId:UUID, rosterId:UUID, scopeFilter:"ActiveDefinitions", balances:WalletLine[] |
| WalletLine | currencyCode:string, displayName:string, scope:Account/Roster/Character, ownerId:string, balance:int64-string, revision:int64-string |
| SessionEvidence | dungeonSessionId:UUID, dungeonId:string, difficulty:string, storedState:기존 상태 enum, updatedAtUtc:UTC, sessionExpiresAtUtc:UTC, memberLeaseExpiresAtUtc:UTC/null, characterLeaseExpiresAtUtc:UTC/null, timing:WithinObservedLease/ObservedExpired/LeaseMissing, settlement:SettlementSummary/null |
| SettlementSummary | storedState:Pending/Processing/Completed/Failed, attemptCount:int, nextAttemptAtUtc:UTC, completedAtUtc:UTC/null, errorCode:string/null |
| OperationLogPage | events:LogEvent[], nextCursor:string/null, coverage:Complete/Partial/Expired/NotInstrumented/Unknown, truncationReason:null/ResultLimit/ReadBudget/Timeout/CorruptRecord/SourceMissing, searchedSources:string[], scannedBytes:int64-string, searchedFromUtc:UTC, searchedToUtc:UTC |

`LogEvent` 필드는 추적 계약의 이벤트 schemaVersion 1을 따른다. 서버 자격·주소·전체 파티·LastError 원문·아이템 seed/전체 stat payload는 위 모델에 없다. 향후 상세가 필요하면 별도 DTO/권한과 검증을 추가한다. `timing`은 저장 시각의 관측 의미이며 온라인 여부나 입장 허용 판정이 아니다.

### 3.3 결과와 오류 코드

오류는 ProblemDetails 형태의 `{type:"about:blank", title, status, code, httpRequestId, traceId, fieldErrors?}`다. fieldErrors는 최대 20개, 메시지는 서버 정의 문구다. 다음 코드를 Web의 한 매퍼에서 처리한다.

| HTTP | code | 처리 |
|---|---|---|
| 400 | `ops.invalid_query`, `ops.invalid_cursor` | 입력 오류, 자동 재시도 없음 |
| 401 | `ops.authentication_required` | 로그인 안내; 보관하던 업무 요청 ID는 유지 |
| 403 | `ops.access_denied`, `ops.csrf_failed` | 권한/요청 위조 거절 |
| 404 | `ops.character_not_found`, `ops.action_not_found` | 단일 대상 부재. 검색 0건과 구분 |
| 404 | `ops.account_not_found`, `ops.restriction_not_found` | 계정/해제 대상 부재 |
| 409 | `ops.request_conflict`, `ops.case_already_compensated`, `ops.target_busy`, `ops.revision_conflict`, `ops.preview_expired`, `ops.policy_changed` | 같은 ID/입력 재조회 또는 화면 재확인; 새 ID 자동 생성 금지 |
| 410 | `ops.cursor_expired` | 처음부터 새 검색 |
| 429 | `ops.rate_limited` | Retry-After 이후 사용자가 재시도 |
| 503 | `ops.auth_provider_unavailable`, `ops.storage_unavailable`, `ops.log_source_unavailable`, `ops.commit_outcome_unknown` | 추적 번호와 결과 상태 표시. 변경의 미확정은 실패 확정과 다름 |
| 500 | `ops.internal_error` | 내부 버그, 안전한 추적 번호만 반환 |

성장 저장이 없으면 progression=Missing, 해석 불가능하면 Unavailable다. 단일 성장 필드 오류로 캐릭터 자체가 없다고 표시하지 않는다. 읽기 취소는 HTTP 연결 취소로 전파하며 지급 여부의 근거로 삼지 않는다.

## 4. Core 포트와 저장 읽기

Core에는 `Result<T>`, 불변 업무 모델, query 값, capability와 아래 포트만 둔다. §3은 Web 직렬화 계약이다. Core에서는 Guid/long/DateTimeOffset/enum을 유지하고 HTTP envelope·문자열 int64·cursor token은 Web 매퍼가 변환한다. 외부 구현이 없다면 인터페이스만 먼저 대량 생성하지 않고 해당 제작 단계에 추가한다.

| 포트 | 정확한 업무 단위 |
|---|---|
| IOperatorAccess | AuthorizeAsync(capability, ct) → Result<OperatorActor> |
| ICharacterReadStore | SearchAsync(CharacterLookup, ct) → Result<Page<CharacterHeader>>; FindAsync(characterId, ct) → Result<CharacterHeader> |
| IInventoryReadStore | ReadAsync(characterId, container, PageRequest, ct) → Result<Page<ItemSummary>> |
| IWalletReadStore | ReadAsync(characterId, ct) → Result<WalletSnapshot> |
| ISessionEvidenceReadStore | ReadAsync(characterId, PageRequest, ct) → Result<Page<SessionEvidence>> |
| IOperationLogReader | SearchAsync(OperationLogQuery, ct) → Result<OperationLogPage> |
| IRewardEvidenceReader — S2 | ReadAsync(characterId, TimeRange, PageRequest, ct) → Result<EvidencePage> |
| ICompensationCommitter — S3 | CommitAsync(CompensationIntent, OperatorActor, ct) → CompensationCommitResult |
| IOperationActionReader — S3 | FindAsync(requestId, ct) → Result<ActionReceipt> |
| IOperationActionReader — S3 | SearchAsync(targetScope, targetId, PageRequest, ct) → Result<Page<ActionReceipt>>; 최근 보상 확인용 |
| IAccountRestrictionReader — S3 | FindAsync(accountId, ct) → Result<RestrictionSnapshot> |
| IAccountRestrictionCommitter — S3 | CommitAsync(RestrictionIntent, OperatorActor, ct) → ActionCommitResult |
| IServiceControlReader — S4 | ReadAsync(ct) → Result<ServiceControlSnapshot>; 공개 응답은 별도 Web DTO로 최소화 |
| IServiceControlCommitter — S4 | CommitAsync(ServiceControlIntent, OperatorActor, ct) → ActionCommitResult |

Web의 cursor 변환은 호스트 Data Protection 어댑터가 맡는다. Core에는 문자열 token이나 보호 키 대신 검증된 페이지 위치 값을 전달한다. cursor는 환경·운영자·query fingerprint·마지막 키·최초 issuedAt/만료(15분)를 보호한다. 새 페이지를 읽어도 최초 만료를 연장하지 않는다.

| 데이터 | PG 읽기/순서 | 주의점 |
|---|---|---|
| 캐릭터 | characters + roster, `(created_at, character_id)` 오름차순 keyset | GetCharacters 자체는 읽기지만 운영 API에 기존 Player 권한을 우회해 호출하지 않음 |
| 성장 표시 | 저장된 character_progression + 기존 GetStoredProgressionSummaries 의미 | [ICharacterProgressionRepository](../Backend/ProjectRpg.Backend/Data/ICharacterProgressionRepository.cs)의 LoadProgressionAsync는 정산을 반영할 수 있으므로 금지 |
| 인벤토리 | owner_type=Character + 정확한 owner_id + container, item_id 오름차순 | 필터별 LIMIT+1로 hasMore 계산. 조회 중 이동은 페이지 간 snapshot 보장 밖 |
| 재화 | 실제 Account/Roster/Character 소유자를 먼저 resolve하고 활성 통화 조회 | 부캐의 같은 원정대 지갑을 독립 재화로 표시하지 않음 |
| 세션/정산 | dungeon_session_members·character_session_leases·dungeon_sessions·dungeon_reward_settlements, `(updated_at,session_id)` 내림차순 | 만료 정리/정산 worker claim/리스 해제 없는 SELECT만 사용 |

Postgres 읽기 어댑터는 READ ONLY 트랜잭션을 사용한다. 한 반환 모델을 여러 SELECT로 조합할 때는 REPEATABLE READ로 묶고 5초 안에 종료한다. 시간 경과로 계산한 표시는 TimeProvider의 고정 now로 평가한다. 여러 페이지/패널 사이의 일관성을 주장하지 않는다. 필요한 인덱스는 owner/container/item_id, 멤버 character/session, 캐릭터 생성순에 맞춰 append migration으로 추가한다.

Memory는 게임 저장소와 같은 잠금 안에서 불변 복사본을 만든다. 조회 때문에 만료 정리·성장 초기화·정산을 하지 않는다. Memory 운영 세션은 개발/Testing에만 허용하며 프로세스 재시작 뒤 재로그인이 필요하다. 저장소 구현 차이를 숨기거나 Memory로 PG 내구성을 증명하지 않는다.

## 5. 운영 세션과 변경 기록의 스키마

아래는 추가할 논리 스키마다. 기존 baseline/v2/v3 SQL을 고치지 않고 [현재 migration 계약](../Backend/ProjectRpg.Backend/Data/PostgresSchemaMigrations.cs)의 다음 미사용 버전에 추가한다. 버전 번호는 병행 작업과 조율하는 배포 식별자이며 스키마 설계 미결이 아니다.

| 테이블/단계 | 필수 열·제약 |
|---|---|
| ops_operator_sessions / S1 | key_hash bytea PK(32바이트), operator_id text, protected_ticket bytea, auth_epoch text, created_at/last_seen_at/absolute_expires_at timestamptz, revoked_at nullable. 만료 인덱스; 원문 세션 키 저장 금지 |
| ops_actions / S3 | request_id uuid PK, operation varchar64, input_hash bytea(32), canonical_input jsonb, operator_id text, case_id uuid nullable, target_scope/target_id, template_version nullable, reason varchar512, committed_at, result_json jsonb, origin_http_request_id uuid, origin_trace_id nullable. 성공 후 수정/삭제 API 없음 |
| ops_compensation_cases / S3 | case_id uuid + target_scope + target_id PK, action_request_id UNIQUE FK ops_actions. 같은 사건/실제 소유자에 재보상 방지 |
| ops_account_restrictions / S3 | account_id PK/FK accounts, revision bigint≥1, state Active/Released, starts_at, expires_at(시작보다 이후), reason varchar512, action_request_id FK. 해제 시 Released와 revision 증가; 원래 ops_actions 보존. 기간 만료는 state=Active AND now<expires_at로 평가하며 조회 중 행 수정 없음 |
| ops_service_control / S4 | singleton id=1, revision bigint≥0, mode Open/Drain, notice_text≤1000, notice_starts_at/ends_at nullable, updated_at, action_request_id FK. 제한 없는 임의 설정 JSON 없음 |

환경별 DB를 분리하므로 request_id의 unique 범위는 그 DB 전체다. 다른 조치에 같은 request_id를 사용하면 충돌이다. 감사 상세는 ops_actions의 확정 입력/결과이며 별도 best-effort audit insert가 아니다. 제품 효과·사건 키·ops_actions를 같은 트랜잭션으로 확정한다. 조회 접근 기록은 기술 로그, 성공한 변경은 이 DB 기록으로 남긴다.

입력 hash는 schemaVersion·환경·operation·검증된 operator·정규화 intent의 고정 필드 순서 UTF-8 JSON을 SHA-256으로 계산한다. UUID/정수 형식은 §3을 따르고 사유는 trim+Unicode NFC, 줄바꿈 LF로 정규화한다. requestId·HTTP/trace ID·preview 발급/만료 시각은 hash에서 제외한다. hash뿐 아니라 canonical_input도 대조한다. 새 요청은 ops 전용 namespace의 requestId advisory lock → 기존 결과 재확인 → 업무 잠금 순서다. 기존 아이템 request 잠금을 앞당기거나 대체하지 않는다. 같은 ID의 병렬 실행은 첫 commit 뒤 같은 결과를 읽는다. 3초 잠금 대기 한도 초과는 target_busy이며 다른 ID로 자동 변경하지 않는다. 트랜잭션 제한은 5초, commit 호출 후 결과를 확인할 수 없으면 commit_outcome_unknown이다.

`ActionReceipt.result`는 operation별 판별 union이다: Compensation={itemId,definitionKey,quantity:1,location,itemReceiptRequestId}; Restriction={accountId,state,revision,startsAtUtc,expiresAtUtc,revokedSessionCount}; ServiceControl={revision,mode,noticeText,noticeStartsAtUtc,noticeEndsAtUtc}. 최초 commit은 201, replay/조회는 200이며 disposition=Committed/Replayed를 envelope에 표시한다. 영수증 부재 404는 아직 처리 중인 요청의 영구 실패 증거가 아니다. 같은 ID 재전송만 허용한다.

운영 세션은 만료 후 24시간에 정리한다. v1의 ops_actions·사건 키·게임 영수증은 자동 삭제하지 않는다. 보존량은 감시하되 상세 아카이브 기능을 구현하기 전 임의로 잘라내지 않는다. 기술 로그 보존 한도와 분리하며, 미래 아카이브도 중복 방지 키/결과 참조를 유지해야 한다.

## 6. 로그의 실제 기록·회전·검색 구현

### 6.1 공급자와 물리 형식

호스트의 ILogger→Serilog 어댑터, `Serilog.Sinks.File`, `Serilog.Sinks.Async`를 사용한다. Core 참조는 추가하지 않는다. NuGet 버전은 구현 시 호스트 TFM과 호환되는 안정 버전을 lock file에 고정하고 이 계약의 기능을 검사한다. 새 로그 엔진/비동기 큐를 직접 만들지 않는다.

- 물리 파일은 UTF-8 JSONL, Serilog JsonFormatter envelope와 허용된 `Properties.OpsEvent`(추적 계약의 schemaVersion 1)다. 전용 sink에는 OperationsEventRecorder가 만든 이벤트만 통과시킨다. 고정 message template·제한된 category와 OpsEvent 외 원문 scope/exception/context를 첨부하지 않는다. 검색 어댑터가 OpsEvent만 읽는다. OAuth callback query/code·요청 body가 일반 HTTP 로깅 경로에도 출력되지 않도록 호스트 필터를 검사한다.
- 허용 목록 이벤트를 생성하는 호스트의 `OperationsEventRecorder`가 현재 Activity·OperationsRequestContext와 안전한 입력을 결합한다. ILogger에 전달하기 전에 마스킹·필드 길이·이벤트 직렬화 16KiB 상한을 검사한다. 초과 상세는 버리고 `detailsTruncated=true`를 기록한다.
- 파일 sink는 UTC 일별 + 32MiB 크기별 회전, `rollOnFileSizeLimit=true`, `retainedFileCountLimit=30`, `retainedFileTimeLimit=14일`, shared=false다. root 하나의 활성 writer는 하나다. 주 로그 파일 30개(약 960MiB)와 메타데이터를 1GiB 예산 안에 두고 초과를 경보한다. 호스트 시간대 UTC를 기동 조건으로 확인한다.
- 비동기 버퍼 4096이벤트, blockWhenFull=false. 드롭 수·sink 오류를 별도 보호된 상태와 외부 감시에 노출한다. 같은 sink로 오류를 무한 재기록하지 않는다. 정상 종료 때 최대 5초 flush를 시도하며 강제 종료/버퍼 유실을 무손실로 주장하지 않는다.
- 읽기 로그와 게임 성공 응답을 디스크 flush 완료까지 기다리게 하지 않는다. 성공 감사 저장 실패는 게임 효과와 함께 롤백하지만, 기술 로그 쓰기 실패는 이미 확정된 지급을 실패로 바꾸지 않는다.

회전/파일 수·시간 보존은 [File sink](https://github.com/serilog/serilog-sinks-file), 제한 버퍼·드롭 감시는 [Async sink](https://github.com/serilog/serilog-sinks-async)의 제공 기능을 사용한다. 1GiB는 본 설계의 예산이며 sink 기본값을 그대로 쓰지 않는다.

### 6.2 검색 알고리즘과 coverage

`LocalJsonlOperationLogReader`는 등록된 root의 정해진 파일 패턴만 연다. root는 웹 정적 경로/Content/Plugins/공유 Saved 로그와 분리하고 원본 경로와 reparse point를 검사한다. sourceId는 서버 등록 식별자이고 브라우저 경로가 아니다.

1. 검색 시작 시 허용 source의 파일 목록·파일 식별자·현재 길이를 고정한다. 이번 검색은 그 끝까지만 읽는다. 이후 append는 새 검색에서 나타난다.
2. 최신 segment부터 역순, segment 안에서는 줄 경계를 찾으며 마지막 완성 JSON 줄부터 역방향으로 읽는다. 한 줄의 최대 물리 크기는 32KiB다. UTF-8 경계/불완전 마지막 줄/잘린 파일은 검출하고 부분 결과로 표시한다.
3. 시간·정확한 ID·EventName/Level을 메모리에서 필터링한다. 기본 100/최대 200건, 3초 또는 64MiB 읽기 상한에서 중지한다. 최대 기간은 24시간, 기본 1시간이다. 최소 하나의 ID 또는 EventName 필터를 요구한다.
4. 결과의 순서는 수집 위치 역순이다. 발생 시각은 별도 표시하고 여러 머신의 전역 시간 순서를 보장하지 않는다. nextCursor는 source/고정 파일 길이·다음 byte 위치·필터·환경·운영자·15분 만료를 보호한다. cursor 최대 8KiB, source/file 수 초과는 400이다.
5. 제한 도달 시 Partial과 이유·이어 읽을 cursor를 반환한다. 파일 삭제/교체 시 cursor_expired, 손상 줄은 건너뛰되 CorruptRecord/Partial과 건수를 표시한다. 전체 source를 열 수 없으면 503이다.

S1의 source는 backend 하나다. S2에서 등록 source를 추가하되 source별 cursor/검색을 사용하고 별도 서버의 임의 파일을 운영 웹에서 직접 열지 않는다. 로그 수집 제공자는 같은 root에 안전한 입력으로 옮기거나 중앙 검색 어댑터를 구현한다. 미등록/미계측 구간은 NotInstrumented/Unknown이다.

한 로컬 source의 catalogue는 최대 30파일이다. API의 ID 필터는 httpRequestId/traceId/operationRequestId/characterId/dungeonSessionId/transactionRequestId/itemId/caseId이며 여러 필터는 AND다. UUID/trace 형식은 고정 검증하고 EventName은 등록 이벤트 키, Level은 정의된 수준만 받는다. 검색 범위는 `[fromUtc,toUtc)`다. 수집 위치 순서상 timestamp가 오래된 한 줄을 봤다고 나머지 파일을 생략하지 않는다.

작은 파일 catalogue는 파일 ID/크기·보존 시작/끝·writer boot ID·마지막 수집·회전/삭제·알려진 유실 구간을 저장한다. 원자적 임시 파일 교체로 갱신하고 부팅 시 파일 stat과 대조한다. catalogue도 1MiB 상한이며 오래된 기술 메타데이터를 정리한다. 신뢰할 수 없는 범위는 Unknown이다. catalogue는 검색 색인이나 게임 상태 저장소가 아니다.

Complete는 고정한 보존 파일을 끝까지 검색했다는 뜻이다. 사건 전부의 수집/보존 성공을 뜻하지 않는다. 검색 요청 자체는 별도 이벤트로 기록하되 현재 검색 snapshot 이후에 추가되어 자기 결과를 무한히 늘리지 않는다.

## 7. S2 보상 조사 계약

`GET /ops/api/characters/{id}/timeline?fromUtc&toUtc&limit&cursor`는 최근 7일/최대 30일·50/200건으로 제한한다. 출력 `EvidencePage={items,nextCursor,hasMore,coverage}`이고 각 항목은 `{sourceKind,sourceKey,occurredAtUtc,characterId?,ownerScope,ownerId,sessionId?,requestId?,itemIds[],status,linkReason}`다. linkReason은 DirectReceipt/SessionMembership/OwnerScope/TechnicalCorrelation 중 하나다.

구현된 S2에서 sourceKind는 CurrencyReceipt/ItemReceipt/Settlement다. `(occurredAtUtc DESC, sourceKind의 위 순서, sourceKey DESC)`로 병합하며 소스별 마지막 반환 위치를 보호 커서에 담는다. 응답에 고정 기간 fromUtc/toUtc를 함께 반환한다. 항목에는 operation·currencyChanges(최대 16개, 모든 int64 문자열)·storedState·detailsTruncated를 추가하고 itemIds는 최대 64개다. `GET /ops/api/characters/{id}/evidence/{sourceKind}/{sourceKey}`는 같은 ReadEvidence 권한과 대상 연결 검사를 거친 안전한 단일 근거 DTO다. 미연결/부재는 404 `ops.evidence_not_found`, 해석 불가능한 원본은 해당 출처 실패다. 결과 소유자 연결만 있는 아이템 영수증에서는 다른 참가자의 itemIds를 제외한다.

coverage는 소스별 `{source,coverage,detailCode}` 배열이다. DB의 Complete는 해당 조회 범위의 저장 기록을 읽었다는 뜻이며 전체 게임 변경 이력을 보증하지 않는다. ItemReceipt는 actor/result-owner로 연결 가능한 범위만 있어 Partial이다. 아직 없는 OperationsActions는 NotInstrumented, 별도로 조회하는 TechnicalLogs는 Unknown이다. 일부 출처 실패 시 해당 출처 Unknown/사유와 다른 출처 결과를 반환하되 cursor를 중단하고, 전체 출처 실패는 503이다. 페이지 간 하나의 DB snapshot이나 소급 로그 계측을 보장하지 않는다.

화면 `/ops/characters/{id}/evidence`는 기간·출처별 상태·원래 영수증/로그 연결을 보여준다. `/ops/characters/{id}/compensation`은 최근 조치→사건/품목/사유→미리보기→실행/미확정→원래 결과 확인 순서다. `/ops/accounts/{accountId}/restriction`은 현재 제한/Revision→기간/사유→확인→실행/결과, `/ops/service-control`은 현재 mode/공지/Revision→수정→확인→실행/결과다. 읽기/확인 GET은 변경을 수행하지 않으며 각 POST/PUT은 앞의 인가/CSRF·원자 계약을 따른다. 각 화면에 결과 대기/거절/저장 장애/원래 요청 재확인 상태를 따로 표시한다.

소스는 실제 [스키마](../Backend/Database/schema.sql)의 세션/멤버/정산·currency_transaction_receipts·item_transaction_receipts와 ops_actions다. 재화는 character_id/committed_at 인덱스, 아이템은 actor_type/actor_id/committed_at/request_id 복합 인덱스, 정산은 character_ids GIN과 시간 조건을 사용한다. 아이템 result_records에는 GIN jsonb_path_ops를 두고 실제 camelCase/문자열 enum의 owner.type/owner.ownerId containment와 시간/건수 조건으로 조회한다. actor 검색과 결과 소유자 검색의 중복은 request_id로 제거한다. 현재 소유자·영수증 결과만으로 모든 이전 소유자를 복원하지 않으며 이 범위는 coverage에 표시한다. 인덱스는 추가 migration으로 관리한다.

- 새 게임 효과/영수증이 발생할 때도 기존 권위 기록이 근거다. 로그 검색 실패가 영수증 조회를 가로막지 않는다. 각 소스를 제한 조회 후 `(occurredAtUtc,sourceKind,sourceKey)`로 병합하고 source별 cursor를 보호한다.
- 이미 존재하는 기록에서 확인 가능한 연결만 반환한다. 오래된 데이터에 원인이 없으면 Unknown이며 가상 거래/세션을 만들지 않는다. 기술 로그 삭제가 DB 영수증 삭제로 전파되지 않는다.
- 조사 상태는 Delivered(확정 영수증), Pending(정산 Pending/Processing), Failed(정산 Failed), Unknown(기록 부족)이다. Delivered 뒤 소비/이동은 별도 사건이고 '현재 가방에 없음'만으로 Failed로 바꾸지 않는다.
- Completed 정산 자체는 개별 지급 영수증이 아니므로 해당 항목은 Unknown이며 저장 상태 Completed를 함께 보여준다. 소비·이동 영수증의 Delivered는 해당 operation이 확정됐다는 의미다. 원래 영수증에 없는 sessionId는 null로 유지하고 현재 잔액/가방만으로 연결을 합성하지 않는다.
- 원래 session/정산/거래 ID를 worker·필요한 UE 갱신 이벤트에 추가한다. 같은 trace가 전파되지 않은 구간은 업무 ID로 연결됐다고 표시한다. 새로운 계측 이전 기간은 자동 소급 완료 처리하지 않는다.

## 8. S3 개별 보상과 제재

### 8.1 단일 아이템 보상

v1 템플릿은 `Ops.Compensation.Item.v1`: Character 소유, 아이템 1종·1개, Mail 컨테이너에 새 첨부 생성. `AllowedDefinitions`에 등록한 서버 정의/버전만 선택하고 가방/재화 직접 변경·자유 stat 입력은 허용하지 않는다. 기본 allowlist는 비어 있어 지급이 비활성이다. 실제 품목은 콘텐츠 설정이며 서버가 검증한 카탈로그에서 선택한다.

- `POST /ops/api/compensations/preview`: `{characterId,caseId,templateId,definitionKey,reason}`. 대상/품목·정책·근거를 읽고 `{normalizedIntent,previewToken,expiresAtUtc}` 반환. preview는 지급/요청 예약을 하지 않는다.
- `POST /ops/api/compensations`: `{requestId,previewToken}`. token은 환경·운영자·정규화 intent·policyVersion·issuedAt/expiresAt을 일반 Data Protection payload로 보호하고 5분 만료를 업무 규칙에서 검사한다. 서명/신원 확인 → 기존 성공 대조 → 새 요청에만 만료/현재 정책 검사 순서다. 새 요청은 만료/정책 변경 시 409, 기존 성공의 동일 요청은 만료 뒤에도 원래 영수증으로 반환한다. token 복호화 키가 없으면 결과 조회 API로 원래 receipt를 확인한다.
- `GET /ops/api/actions/{requestId}`: `{requestId,operation,committedAtUtc,operatorId,target,caseId,result,effectReferences}`. 미확정 시 같은 ID로 조회/재전송하며 새 ID를 자동 발급하지 않는다. 화면은 전송 전에 환경/운영자별 최근 requestId 최대 20개를 localStorage, token은 sessionStorage에 보존한다. 로그아웃 때 token은 지우되 요청 번호로 재로그인 후 조회할 수 있다. DB 자격·OAuth token을 브라우저 저장소에 보관하는 용도가 아니다.
- `GET /ops/api/actions?targetScope=Character&targetId&limit&cursor`는 최근 확정 조치를 `(committedAtUtc,requestId)` 역순의 Page<ActionReceipt>로 반환한다. 동일한 50/200건·15분 cursor 계약과 S3 권한을 적용한다. `ops_actions(target_scope,target_id,committed_at,request_id)` 인덱스를 둔다.
- CaseId는 조사 사건 UUID, reason은 공백 제외 10~512자. 같은 사건+Character에는 한 번만 지급한다. v1에서 같은 사건의 추가 지급 override는 제공하지 않는다. 별도 실제 사건은 새 근거/CaseId로 처리하며 최근 보상 이력을 항상 표시한다.
- 활성/경합 리스, Waiting/Loading/InProgress/SettlementPending 멤버 세션, 해당 캐릭터의 Pending/Processing 정산 중 하나라도 있으면 target_busy로 거절한다. 만료 시각이 지났어도 저장된 비종료 세션을 운영 지급이 정리하지 않는다. Failed 정산이라도 확정 영수증이 존재할 수 있으므로 성공 여부를 조사한 뒤 사유를 기록해야 한다. 자동 재보상은 없다.

원자 경계: ops requestId 잠금/기존 성공 대조 → character FOR UPDATE → 기존 순서의 item owner/아이템 request 잠금 → lease FOR UPDATE NOWAIT → 멤버가 속한 비종료 dungeon_sessions를 session ID 순서로 FOR UPDATE NOWAIT → 미정산 재검사 → 서버 생성 item ID/첨부 → 아이템 영수증·ops_actions·사건 unique 키 함께 commit. 기존 장비 처리의 [character/owner/lease 순서](../Backend/ProjectRpg.Backend/Data/PostgresGameRepository.EquipmentProgression.cs)를 유지한다. lease/session NOWAIT 실패는 rollback 후 target_busy다. 부재 리스/새 멤버의 FK 삽입은 character 잠금과 경합한다. 정산 enqueue는 session 부모 잠금 아래 InProgress를 확인하므로, 운영 지급은 같은 부모 잠금과 비종료 상태 거절로 경합을 닫는다. 기존 정산이 Pending/Processing이면 worker가 owner 잠금을 놓았어도 거절한다. 원래 상태가 종료됐고 처리할 정산도 없을 때만 새 첨부를 만든다. 기존 세션을 종료 상태에서 다시 활성화하는 경로가 추가되면 이 계약도 함께 수정한다.

단일 캐릭터/사건 unique 경합은 rollback 뒤 기존 action을 읽어 case_already_compensated로 돌려준다. item_receipt와 ops_action 사이 불일치는 internal_error로 중단하고 원래 저장 근거를 조사한다. 개발 Memory 어댑터도 같은 논리 검사·효과/감사/사건 키를 하나의 잠금에서 원자 적용하되 프로세스 재시작 내구성을 보장하지 않는다.

첨부 위치는 `Mail/OpsCompensation.<requestId>/0`, CreationSource/operation은 `Ops.Compensation.Item.v1`이다. 아이템 영수증의 Actor는 실제 Character 소유자, 운영자 신원은 ops_actions에 저장한다. 기존 서버 아이템 생성 규칙으로 정의 버전의 기본 속성·서버 seed를 생성하고 item ID/seed/확정 결과는 첫 commit 영수증에 보존한다. 서버 생성 규칙이 없는 품목은 allowlist에 등록할 수 없다. replay는 같은 ID/원래 결과다. 변경 완료 직후 즉시 UE 갱신을 약속하지 않으며 다음 로비/우편 재조회에 나타나는 것을 v1 반영 계약으로 한다. 일반 던전 보상의 인벤토리 우선 규칙은 유지한다.

### 8.2 기간 제재/해제

Backend/운영 화면과 Memory/PG/HTTPS 검증은 구현했다. [실행 및 남은 UE 검증](OPERATIONS_TOOL_RUNTIME.md#기간-제재해제s3-backend운영-화면)을 따른다. v7은 기존 캐릭터 target_id/사건 키를 보존하고 Account 조치용 target_account_id를 추가하며 대상 모양을 제약한다. 계정 조치에서 사건/템플릿은 해당하지 않는다.

`POST /ops/api/account-restrictions`는 `{requestId,accountId,expectedRevision,expiresAtUtc,reason}`를 받고 기본 24시간/최대 30일이다. 영구 제재는 v1에서 제외한다. `POST /ops/api/account-restrictions/{accountId}/release`는 `{requestId,expectedRevision,reason}`를 받는다. 결과/재시도는 ops_actions와 동일 규칙이며 해제도 별도 영수증을 남긴다.

`GET /ops/api/account-restrictions/{accountId}`는 RestrictionSnapshot={accountId,state:None/Active/Released/Expired,revision,startsAtUtc?,expiresAtUtc?,reason?,actionRequestId?,observedAtUtc}를 반환한다. 계정은 있지만 기록이 없으면 None/revision=0이다. 새 제한은 expectedRevision=0, 저장 revision=1이다. 기존 제한 갱신/해제는 현재 revision 일치를 요구하며 +1한다. startsAt은 서버 now, expiresAt 생략 시 hash에는 Default24Hours 표식을 넣고 첫 commit에서만 now+24시간을 계산한다. 명시한 만료는 now보다 이후/30일 이하다. 이미 해제/만료된 제한의 해제도 기대 revision이 맞으면 감사가 있는 Released 상태로 기록한다. 없는 계정/없는 해제 대상은 404이며 reason 규칙은 보상과 같다.

계정 제한과 기존 auth_sessions 폐기는 한 트랜잭션이다. 새 Steam 로그인/세션 발급, 새 입장 티켓 발급·소비·입장 확인에서 제한을 확인한다. 이들 경로와 제재 변경은 같은 계정용 transaction advisory lock을 게임 행 잠금보다 먼저 획득하도록 공통 저장 경계에 연결한다. key는 고정 namespace+계정 ID의 안정 해시이며 충돌은 직렬화만 늘리고 다른 계정 권한을 주지 않는다. 여러 계정은 정렬한 ID 순서다. ops 요청 잠금이 필요한 경로는 requestId → service gate(S4) → 계정 gate → 게임 행/owner 순서다. 단순 UI 검사나 '검사 후 다른 트랜잭션에서 입장'은 허용하지 않는다.

현재 확정 입장한 전투 연결은 강제 종료하지 않는다. 해당 게임 서버의 기존 heartbeat·완료 보고·정상 정산은 허용한다. 플레이어 API 인증 폐기와 신규/재접속 차단은 즉시 적용한다. 만료/해제 후에도 옛 auth session을 되살리지 않고 재로그인한다. 실제 UE의 인증 만료 처리와 남은 파티/보상 보존을 검증해야 기능 완료다.

## 9. S4 공지·점검·알림·복구 정책

### 9.1 서비스 제어

`PUT /ops/api/service-control`은 `{requestId,expectedRevision,mode:Open|Drain,noticeText,noticeStartsAtUtc?,noticeEndsAtUtc?,reason}`를 받는다. notice는 최대 1000자 plain text, 시작<종료, 링크/HTML 실행 없음. 현재 표시 여부는 UTC로 계산하고 별도 예약 worker를 만들지 않는다. service-control과 성공 감사는 동일 트랜잭션이다.

2026-11-08: Backend/운영 화면·익명 상태 조회·신규 입장 원자 가드를 구현했다. 기본 쓰기 비활성, v9 migration·복구 UI·검증 범위는 [실행 계약](OPERATIONS_TOOL_RUNTIME.md#공지opendrain-서비스-제어)을 따른다. 공지·점검의 UE 로비 표시/실제 HTTP 연결과 장기 Drain 경보도 구현했다. native 위젯/인증 캐시/캐시 수명 검증과 실제 GPU·Steam·게임 소켓 전투의 경계는 [로비 계약](ONLINE_LOBBY_UI.md#서비스-공지와-점검-안내)을 따른다. 실제 배포는 후속이다.

초기 singleton은 Open/revision=0/빈 공지이며 action 참조는 초기 행만 null을 허용한다. 변경은 expectedRevision 일치와 +1을 요구한다. `GET /ops/api/service-control`로 현재값/관측시각을 읽고 같은 변경 전에 재확인한다. `GET /api/service-status`는 익명·읽기 전용으로 mode/공지/표시 예정 시각만 제공하며 운영자/감사 정보는 제외한다. 현재 ApiAuthenticationMiddleware는 steam-ticket 외 `/api`에 인증을 요구하므로 S4에 이 정확한 GET 한 개의 예외와 IP당 60회/분 제한을 추가한다. 다른 메서드/하위 경로는 예외가 아니다. UE 로그인·로비가 이를 표시하고 서버의 503 `service_draining` 거절을 안내한다. 공지 시각 둘 다 null이면 즉시·무기한, 한쪽만 있으면 열린 구간이다. 빈 noticeText는 공지 숨김이다.

Drain은 신규 로그인·던전 생성/참가·출발·새 티켓/입장 확인을 거절하고 기존 확정 세션의 전투·heartbeat·정산을 유지한다. 이미 인증된 유저의 조회·우편 수령은 허용한다. 기다리던 방/리스는 임의 삭제하지 않으며 점검 사유와 재시도 안내를 표시한다. 제어 전환과 새 접근은 공통 service gate를 같은 트랜잭션에서 확인한다. service gate는 고정 namespace의 transaction advisory lock이며 접근은 shared, 전환은 exclusive로 두어 정상 입장끼리 전역 직렬화하지 않는다. 잠금 순서는 ops requestId(해당할 때) → service gate → 계정 gate → 기존 게임 잠금이다. 요청 조회/기존 전투 정산은 gate를 요구하지 않는다.

운영 웹에는 강제 종료/전체 DB 복원 버튼을 두지 않는다. 배포 절차는 Drain → 활성 던전/처리 중 정산 소진 확인 → 외부 배포 수단으로 생산자·worker·백엔드 순서대로 중지/쓰기 차단 확인 → 백업/배포/복구 → readiness·계약 검사 → Open이다. 30분을 넘어도 자동 강제 종료하지 않고 알림을 보낸다. Failed/미확정 정산은 해결/보존 방식을 확인한 뒤 중지하며 '시간이 지났으니 안전하다'고 처리하지 않는다.

### 9.2 알림

런타임·정산·로그·장기 Drain·백업 감시와 외부 HTTPS webhook 전달/영속 재전송은 구현했다. [실행 범위](OPERATIONS_TOOL_RUNTIME.md#런타임-감시알림s4-독립-범위)를 따른다. `service_drain_long`은 첫 정상 Drain 관측부터 30분 초과에 경보, 30분마다 지속 요약, 두 번 연속 Open에 복구를 전송한다. 관측 구간/사건은 영속하며 공지 편집은 타이머를 초기화하지 않는다. Unknown/2분 초과 공백은 미확정 구간을 다시 측정하고 활성 경보를 해제하지 않는다. [세부 관측·배포 계약](OPERATIONS_TOOL_RUNTIME.md#장기-drain-경보).

외부 감시 프로세스가 30초마다 health/ready를 5초 timeout으로 확인한다. 3회 연속 실패 시 알림, 2회 연속 성공 시 복구 알림, 지속 장애는 30분마다 요약한다. Failed 정산 신규는 발견 즉시(정상 감시 주기 30초 이내), Pending/Processing 2분 초과는 3회 관측 시, 로그 드롭/공간 예산 초과·백업 실패는 발견 시 알림이다.

감시는 backend와 다른 관리 호스트에서 실행한다. `GET /internal/operations/status`는 management ingress 전용·별도 `OperationsMonitor` 기계 인증(전용 비밀 bearer, 상수 시간 대조)으로만 접근하며 분당 30회로 제한한다. 이 자격은 `/ops`나 게임 API 권한을 부여하지 않는다. 응답은 observedAt/bootId·ready·정산 Pending/Processing/Failed 수와 가장 오래된 지연/latestFailedUpdatedAt·로그 droppedCount/sinkErrorCount/retainedBytes/lastWriteAt·serviceControl.mode이며 ID/본문/SQL은 없다. DB/서비스 조회 장애는 Unknown/503이다. 감시가 수집 실패도 경보하므로 backend 종료 시 마지막 정상값을 최신으로 표시하지 않는다. Failed 수 증가/최신 Failed 시각 전진을 새 장애로 알리고 최초 관측에 Failed가 남아 있어도 알린다. 상세 조사는 사람이 운영 로그인으로 수행한다.

DB 호스트의 60초 상태 수집 작업은 pgBackRest 결과/마지막 full·incremental·archive 성공/실패·미전송 WAL 상태를 작은 원자 교체 JSON에 기록한다. 외부 감시는 관리용 읽기 전용 경로로 이 상태만 읽으며 2분 갱신 중단/일 incremental 성공 26시간 초과/주 full 성공 8일 초과도 경보한다. 쓰기가 없는 정상 DB는 archive 시각만 오래됐다고 실패로 판정하지 않는다. 상태 전달 경로·기계 자격은 배포 입력이고 외부 서버 중단 감지와 별개로 복구 리허설에서 전달 실패까지 검사한다.

2026-11-08: Linux pgBackRest 작업 결과 ledger/상태 수집과 monitor 파일 reader·순수 경보 정책을 구현했다. 환경/서비스·stanza/cluster 대조·WAL 실제 대기열/실패·full2체인·독립 backend 장애·Unknown/복구를 다룬다. [실행/권한/검증 계약](OPERATIONS_BACKUP.md)을 따른다. 실제 운영 원격 repo/전달/타이머·복원/PITR·RPO/RTO 달성 검증은 후속이다.

전달은 설정된 HTTPS webhook 어댑터 하나로 시작한다. payload는 `{eventId,environment,service,incidentKey,severity,state,firstSeenAtUtc,observedAtUtc,summary,referenceIds}`; 비밀값/원문 개인정보는 제외한다. 외부 감시가 incidentKey/마지막 전달을 작은 영속 상태로 보관하고 1/5/15분 간격 최대 3회 재전송한다. 중복 전달을 허용하는 같은 eventId를 사용한다. 관리 URL/인증 헤더는 배포 비밀값이며 수신 endpoint 외 임의 URL 전송을 허용하지 않는다.

### 9.3 백업과 복구

운영 PostgreSQL은 Linux DB 실행 환경의 pgBackRest를 사용한다. 주 1회 full, 매일 incremental, 연속 WAL archive와 PostgreSQL `archive_timeout='5min'`, 성공한 full 체인 최소 2개와 그 체인에 필요한 WAL을 보존한다. DB 서버와 분리된 암호화 backup repository를 사용한다. 백업 도구의 원자성/보존/복원 기능은 [pgBackRest 공식 지침](https://pgbackrest.org/user-guide.html)을 따른다. 저빈도 쓰기에서도 WAL segment 전환을 제한하는 설정의 의미는 [PostgreSQL archive_timeout](https://www.postgresql.org/docs/current/runtime-config-wal.html#GUC-ARCHIVE-TIMEOUT)을 따른다.

서비스 목표는 **RPO 15분, RTO 4시간**으로 정한다. WAL archive/저장소 연결 실패를 5분 안에 알리고, 실제 DB 크기로 별도 복구 리허설에서 목표를 검증한다. 목표 수치는 이미 달성했다는 주장이 아니다. 최초 서비스 전, 주요 schema/backup 설정 변경 후, 이후 월 1회 복구 확인한다.

복구는 점검/생산자 중지 확인 → 별도 인스턴스에 지정 시점 복원 → 계정·아이템·재화·영수증·처리 중 정산 검사 → 운영 세션 폐기/AuthEpoch 교체·옛 플레이어 인증/미사용 티켓/게임 서버 자격 폐기 → 앱의 동일 migration 이력 확인 → 미확정 요청을 원래 ID로 복구 → 연결 전환/정상 확인 순서다. 복원된 리스/Processing 작업은 기존 복구 규칙으로 대조하고 상태/영수증을 임의 삭제하지 않는다. 복원으로 사라질 수 있는 복원 시점 이후 지급 요청은 격리한 장애 원본/사건 기록과 대조하기 전 자동 재전송하지 않는다. 복구 중 생성한 사본은 자기 소유와 사용 해제를 확인한 뒤 정리한다.

2026-11-08: [별도 복원/PITR 리허설 도구](OPERATIONS_RECOVERY.md)를 구현했다. 새 비공개 작업공간·기본 테이블스페이스·읽기 전용 검사·목표 pause·동일 migration/전체 public 지문·안전 정지/폐기를 제공한다. 실제 암호화 backup의 정상3종/실패4종과 원본 데이터/repo 보존을 격리 DB에서 검사한다. 실제 운영 목적지/자격 폐기/앱 재시작/연결 전환·운영 크기 RPO/RTO는 별도 단계다.

## 10. 설정과 기동 검증

설계가 정한 기본값과 실제 배포 때 넣는 값을 구분한다. 비밀값은 환경/비밀 저장소에서 공급하며 예시 문서에 실값을 쓰지 않는다.

| 설정 묶음 | 기본값/제약 | 배포 입력 |
|---|---|---|
| Operations | Enabled=false, 활성 호스트 1개 | EnvironmentId, ManagementOrigin, 허용 proxy/관리 네트워크 |
| Authentication | GitHub OAuth, 30분/8시간, PKCE, 허용 ID 1개 | ClientId/Secret, AllowedGitHubUserId, AuthEpoch, callback URL, 키 보호 경로 |
| Reads | 50/200건, DB 3초/전체 5초, cursor 15분 | 변경 필요 없으면 기본값 |
| Logs | File/Async, 32MiB·30파일·14일, JSONL, 4096큐, 검색 3초/64MiB/100~200건 | OpsLogRoot, sourceId, 파일 시스템 권한/영속 경로 |
| Compensation | 템플릿 고정·1개·preview 5분, AllowedDefinitions=[] | 실제 지급 허용 품목 카탈로그. 비어 있으면 지급 비활성 |
| Alerts | 30초 감시/3회 실패/2회 복구, 제한된 기계 상태 조회 | 외부 monitor 위치, MonitorToken, 백업 상태 읽기 전용 경로, webhook URL/secret |
| Backup | pgBackRest, 주 full/일 incremental/WAL, RPO15분/RTO4시간 | repository 주소·암호화/접근 자격·복구 대상 인프라 |

Ops 활성 상태에서 인증·키 보호·DB 세션·영속 로그 root가 잘못되면 기동 검증에 실패한다. gameplay 자체의 설정/기능은 Ops 비활성 시 추가 자격을 요구하지 않는다. S3/S4 경로는 해당 단계 활성화와 카탈로그/전달 설정 검증 후만 등록한다. 설정의 schemaVersion과 app build를 기록하며 패키지/TFM은 지원 중인 호스트 조합을 배포 게이트에서 검사한다.

## 11. 요구사항에서 구현 검증으로 연결

아래 표는 설계의 완료 확인이다. 마지막 열은 **구현 후 실행할 검증**이며 이번 문서 작성에서 통과했다는 뜻이 아니다.

| 요구/결정 | 책임·계약 | 구현 시 합격 증거 |
|---|---|---|
| SOLID / D03 | Core 포트, Web/Infrastructure/Hosting, 구체 class 유스케이스 | 금지 참조/서비스 탐색 없음, 저장/로그 어댑터 교체 계약 검사 |
| 개인 인증 / D01,D02 | §2 + ops_operator_sessions | 허용/다른 계정·PKCE/state·401/403·CSRF·idle/absolute·logout·재시작/복구·공급자 장애 |
| 읽기 / D04 | §3 DTO·§4 포트/읽기 snapshot | 만료 세션/성장 Missing/Unavailable을 조회해도 게임 테이블 불변, int64·cursor·부분 실패 |
| 추적 / D05 | §6 + 공통 추적 계약 | 오류 화면 번호 검색, 동시 scope 분리, 회전/재시작/드롭·손상·잘림·마스킹·권한 |
| 보상 조사 | §7 EvidencePage | 직접 영수증/소유 범위/기술 연계 구분, 소스 누락·이미 소비된 아이템 오판 없음 |
| 원자 지급 / D06 | §5·§8.1 | PG 동시 같은/다른 ID·사건 unique·preview 만료·리스/정산 경합·감사 실패 롤백·응답 유실 replay·UE 우편 재조회 |
| 제재 / D07 | §8.2 계정 gate | 로그인/티켓/확인 경합, 세션 폐기·기간 만료·해제, 기존 파티/정산 보존 |
| 점검 / D08 | §9.1 service gate | Drain과 신규 입장 경합·기존 진행 유지·notice 시간/Revision·배포 전 쓰기 중지 |
| 서비스 운영 / D09 | §9.2~3 | 외부 outage/복구 알림, webhook 중복/실패, 실제 PG 복원·RPO/RTO·미확정 요청 보존 |

## 12. 설계 완료와 구현 인계

- **완료한 결정:** v1 기능 범위, 모듈/포트, 개인 인증·세션·권한 경로, S1 화면/DTO/API/오류·페이지 처리, 실제 로그 저장/검색/회전, S2 증거 조회, S3 변경/제재의 원자 경계, S4 점검/알림/백업·복구와 검증 기준.
- **배포 때 입력할 값:** 실제 계정/앱 자격·도메인·경로·카탈로그 품목·webhook/backup 자격. 자격 발급이나 실제 인프라 배치가 아직 없다는 이유로 동작 계약을 다시 열지 않는다.
- **구현 때 검증할 사실:** 실제 라이브러리 버전/호스트 호환성, 기존 코드와 잠금 경합, 실제 DB/UE 연결·성능·복구 목표. 검증이 설계 가정을 반증하면 해당 결정과 영향을 문서화해 수정한다.
- **v1 밖:** S5 대량/예약·직원 권한 UI·자유 편집/회수·전체 경제 대시보드. 별도 기능 요청 시 설계한다.

설계 단계의 남은 미결 항목은 없다. S1 기본 조회·인증·추적과 S2 보상 조사·worker 원래 ID 계측, S3 개별 아이템 보상·기간 제재/해제의 Backend/운영 화면, S4 런타임 감시/알림과 공지·Open/Drain Backend/운영 화면을 구현했다. [실행/검증 경계](OPERATIONS_TOOL_RUNTIME.md)를 따른다. S4 공지/점검의 UE 로비 표시와 HTTP 연결·장기 Drain 경보·백업 상태 수집/경보·격리 복구 리허설, 선택형 게임 API HTTP 추적과 아이템·재화 거래 영수증 연결도 구현했다. 그 밖의 게임 업무/저장 구간·UE 전체 사건 계측, 운영 용량/외부 환경 복구 리허설과 운영 배포는 후속이다. 고정 사본의 UE 인증/문맥 검증을 실제 게임 소켓 전투·Steam 검증으로 확대하지 않는다.
