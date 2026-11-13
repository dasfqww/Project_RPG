# 플레이어 인증의 시계 오차·변경 경계

2026-10-08: 플레이어 로그인 응답에 서버 발급 시각 `issuedAt`을 추가하고, UE 인증
만료 판단을 클라이언트 UTC 벽시계 대신 프로세스의 단조 증가 시간으로 전환했다.

## 계약

### UE HTTP 구현 파일 경계 (2026-11-13)

`UHttpWebManager`의 공개 API·상태 소유권은 유지하고 구현을 기능별로 나눴다.
`Private/Manager/HttpWebManagerPlayer.cpp`는 로그인·로그아웃·응답의 인증 상관 검사와
캐릭터 조회/생성을, `HttpWebManagerDungeonSession.cpp`는 세션 조회·참가·출발·포기와
서버 시작/heartbeat/종료를, `HttpWebManagerRewards.cpp`는 정산과 보상 스냅샷을 담당한다.
기존 `HttpWebManager.cpp`는 subsystem 수명·공통 헤더·입장 티켓·연결 복구/멤버십을 유지한다.
서버 실행 환경 판정은 private `HttpWebManagerRuntime.h`의 동일한 inline 함수로 공유하며,
복구 진단 콘솔 명령은 기존 CPP에 한 번만 등록한다. 기존 70개 멤버 함수와 네 helper의
본문을 보존했으며 인증 시계·요청 세대·서비스 자격·정산 영수증의 규칙을 바꾸지 않았다.

분리한 실제 CPP 네 개는 게시 소스의 격리 사본에서 전체 프로젝트 UHT 후 개별 컴파일로
확인한다. 이 검사는 모듈 링크·새 DLL 설치·Steam/게임 실행 검증과 구분한다.

후속 책임 분리에서는 정산 본문·진행 요청·응답/취소를 `FRPGDungeonRewardSettlement`로
이관했다. 현재 `HttpWebManagerRewards.cpp`는 기존 API의 위임·좁은 host 구현과 스냅샷
조회를 담당한다. 인증 시계/토큰 소유권은 유지하며 정산 객체의 경계와 집중 검사는
[보상 전송 계약](DUNGEON_REWARD_LIVE_REFRESH.md#ue-정산-전송의-책임-2026-11-13)을 따른다.

세션 생성/조회/출발·서버 수명 요청과 응답 해석은 후속 `FRPGDungeonSessionTransport`로
이관했다. `HttpWebManagerDungeonSession.cpp`는 기존 API/host 위임과 포기 처리를
유지하며 공통 인증/던전 세대 규칙을 바꾸지 않는다. 세션 데이터 타입은 이름/필드를
보존해 `Online/RPGDungeonSessionTypes.h`로 옮겼다.
[세션 HTTP 경계](ONLINE_LOBBY_UI.md#세션-http-처리-경계-2026-11-14)를 따른다.

서버 멤버십 GET/단절 만료 POST의 공통 진행 요청·취소/종료 상태는
`FRPGDungeonMembershipTransport`로 이관했다. 공통 플레이어 헤더와 독립된 기존
서버 자격 검증/적용을 유지한다. [멤버십 전송 경계](DUNGEON_ABANDONMENT.md#ue-멤버십-전송의-책임-2026-11-14)를 따른다.

### 인증 요청의 수명과 소유권 (2026-11-13)

`FRPGBackendAuthentication`이 로그인/로그아웃의 진행 요청·로그인 시작 단조 시각·
로그아웃 문맥 검사/완료 callback·취소를 소유한다. 기존 Manager API와 delegate는
유지하고 객체에 위임한다. `FRPGBackendPlayerAuthState`의 토큰·만료·계정 고정 규칙과
Manager의 소유권은 그대로이며, 서비스는 같은 메모리 상태를 참조한다.

호스트는 현재 endpoint/timeout·서버 실행 여부·진행 중 성장 요청과 기존 헤더 정책·
Drain 관측·결과 알림·던전 callback 무효화만 제공한다. 게임/UI 로그아웃 자격은
기존 호출자가 전달하는 문맥 검사에 남는다. 장비/성장의 로그인·로그아웃 차단 조건도
객체의 동일한 진행 상태를 읽으며 새 허용 조건을 만들지 않는다.

로그인 중 중복 호출은 원래 요청을 취소하거나 실패 알림을 보내지 않는다. 응답은
현재 요청과 일치해야 Drain 관측/자격 적용/알림으로 진행한다. 로그아웃은 같은 자격의
정확한 204/401과 원래 문맥 확인 뒤에만 계정 고정을 해제한다. 확인 중에는 로그아웃을
진행 상태로 유지하고, 완료 알림 전에 슬롯을 해제해 명시적인 후속 로그인을 허용한다.
서비스/호스트 약한 참조와 요청 일치 검사가 취소·이전 요청·서비스 종료 후의 callback을
차단한다. 종료 시 원래 순서로 로그아웃/로그인을 취소하며 토큰을 파일에 저장하지 않는다.

`Scripts/Tests/Run-AuthenticationServiceTests.ps1`은 서비스와 기존 인증 상태를 격리
모듈로 링크해 로그인/응답 상관·잘못된 자격/지연·로그아웃 결과/문맥·동시 요청 차단·
취소/수명·재진입을 검사하고, 전체 프로젝트 UHT와 Manager/기존 영향 검사 CPP를
컴파일한다. 주입 응답 검사이며 실제 Steam/HTTP·설치 DLL/전체 게임 실행과 구분한다.

`POST /api/auth/steam-ticket`의 성공 응답은 기존 `accessToken`, `expiresAt`, `steamId`와
새 `issuedAt`을 포함한다. `issuedAt`과 `expiresAt`은 동일한 서버 시각 표본에서 만들며,
기존 DB 토큰 hash·만료 저장, 서버의 실제 만료/401 검증은 변경하지 않았다.
기존 클라이언트가 추가 필드를 무시할 수 있는 additive 응답 변경이다. 새 UE 클라이언트는
네 필드 모두를 요구하므로 백엔드를 먼저 배포해야 한다. 누락 시 로컬 UTC 비교로
되돌아가거나 무기한 인증하지 않고 로그인 실패로 처리한다.

만료 deadline은 다음처럼 계산한다.

```text
유효 기간 = 서버 expiresAt - 서버 issuedAt
로컬 deadline = 로그인 HTTP 요청 시작의 FPlatformTime::Seconds() + 유효 기간
남은 시간 = deadline - 현재 FPlatformTime::Seconds()
```

서버가 실제로 토큰을 발급하기 전에 요청이 시작되므로 요청·응답의 전체 지연을
유효 기간에서 차감하는 보수적 계산이다. 응답 수신 시각부터 유효 기간을 다시 부여하지
않는다. 요청 지연이 유효 기간을 모두 사용한 응답은 거절한다. 네트워크 지연이 크면
서버 만료보다 조금 일찍 재로그인을 요구할 수 있지만, 지연만큼 인증을 늘리지 않는다.

- 두 서버 시각 모두 ISO-8601의 명시적 `Z`/UTC offset이 필요하다.
- 유효 기간은 양수이고 백엔드 플레이어 최대 설정과 동일한 24시간 이하다.
- 요청/수신 시간은 유한한 값이며 요청 시작 이전의 수신 값은 거절한다.
- 정확한 deadline부터 인증되지 않은 상태다. 관측 후 시간을 뒤로 돌려도 토큰은 부활하지 않는다.
- 단조 증가 카운터가 역행하거나 비유한 값이 되면 재로그인 전까지 실패 상태를 유지한다.
- 만료/401 후 SteamID 및 진행 중 요청의 상관 정보는 유지한다. 기존 동일 계정 재인증,
  다른 계정 거절, 이전 토큰 응답 무시 및 UI busy 해제 계약은 유지한다.
- 인증 성공만으로 이동/정산/보상을 처리하지 않는다. 던전 결과 재조회와 별도 복귀 HTTP의
  최신 `Cleared`/`Failed` 확인은 그대로다.

Windows UE 5.8 로컬 구현의 `FWindowsPlatformTime::Seconds()`는
`QueryPerformanceCounter`를 사용한다. 시스템 시각/시간대 변경과의 독립성은
[Microsoft QPC 문서](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)를 따른다.
프레임 delta나 월드 시간을 사용하지 않으므로 일시 정지·time dilation으로 유효 기간을
늘리지 않는다. 토큰 및 deadline은 메모리에만 저장하며 프로세스 재시작 시 복원하지 않는다.

기존 `SetAccessToken`/opaque-token 호환 API에는 메타데이터가 없다. 이 경로는 정상
플레이어 로그인에 사용하지 않으며 이번 유효 기간 보정을 보장하는 경로도 아니다.
전용 서버 서비스 토큰과 Join Ticket의 서버 권한/유효 기간도 이번 변경 대상이 아니다.
우편 첨부 등 다른 기능의 날짜 표시/만료 UI를 일괄 보정하는 변경도 아니다.

## 회귀

```powershell
dotnet run --project Backend/ProjectRpg.Backend.TokenTests/ProjectRpg.Backend.TokenTests.csproj
& .\Scripts\Tests\Run-DungeonClientFlowAutomationTest.ps1
```

백엔드 6개 회귀는 시각 표본 1회, 설정 유효 기간, 게임 서버/입장 티켓 기존 기간,
토큰 randomness/hash/header 안전성, JSON 네 필드 계약 및 과거/미래 시각의 상대 기간을 검사한다.
DB나 OS 시계를 변경하지 않는 순수 테스트다.

UE `DungeonAuthenticationClock`은 요청 지연 차감·정확한 만료·만료 뒤 역행·카운터 이상,
24시간 상한·누락/모호한 메타데이터·동일 계정 갱신을 검사한다. 기존 transport 회귀도
실제 manager callback에 과거/미래 시각의 응답을 전달하고 `issuedAt` 누락/과도한 지연
거절·기존 인증 보존·이전 응답 무시를 검사한다. 테스트가 실제 PC 시각을 변경하지 않는다.

실제 Steam, Shipping, 운영 TLS/네트워크 시간 동기화, OS suspend/resume 및 다른 플랫폼의
타이머 특성, 60분 자연 경과 검증은 별도다. 이 변경은 클라이언트의 만료 UX를 보강하며
클라이언트 값을 서버 권한의 근거로 사용하지 않는다. 인증 서버 불통/연결 단절의 강제
로비 탈출·`InProgress` 중 포기·로그아웃/계정 전환 정책은 여전히 남는다.

## 검증 기록 (2026-10-08)

- 백엔드 Debug 빌드 및 토큰 메타데이터 6개 회귀:
  `Saved/Tests/AuthenticationClock/94ddc5e1e9cb47e2ab75046ea59649bf/backend-build.log`,
  `token-tests.log` 성공. 경고·오류 0개이며 토큰/직렬화 본문을 출력하지 않는다.
- 최초 DB 회귀는 Docker Desktop과 PostgreSQL이 종료돼 접속 단계에서 실패했다.
  `SchemaTests-FirstAttempt-Environment.txt`에 환경 진단을 기록했다. 기존 Docker Desktop을
  hidden으로 시작해 기존 컨테이너/데이터를 준비했으며 DB 파일·볼륨은 삭제/재생성하지 않았다.
- 복구 후 스키마 10개·보상/권한 52개·DB 장애 6개 회귀 성공. 위 디렉터리의
  `SchemaTests-Verified.log`, `RewardDeliveryTests-Verified.log`, `HealthTests-Verified.log`에
  기록했으며 토큰 6개를 포함해 백엔드 총 74개 회귀가 통과했다. 각 격리 Debug 빌드의
  경고·오류는 0개다. 이번 실행의 임시 스키마 3개가 남지 않았음을 DB에서 재확인했다.
- 전체 HTTP 정산 8사례·백엔드 재시작 후 인증/스키마/보상·스토리지 준비·경제/아이템·
  보안 증거 복구 성공: 위 디렉터리의 `integration-result.json`, RunId `52ea3f2723e2`.
  보존 로그는 `%LOCALAPPDATA%/Temp/project-rpg-postgres-6a24742f8de640ebb0cc632b30f1afca/`에
  있으며 두 서버 기동에서 schema ready를 확인했다. 초기화 교착/Unhandled exception은 0건이다.
- 첫 UE 빌드 `Saved/Logs/AuthenticationClockBuild-20261008.log`는 공유 피해 guard의
  `const FScriptArrayHelper::GetRawPtr` 호출 오류와 테스트 생성 헤더 불일치로 실패했다.
  helper의 로컬 wrapper만 non-const로 수정했다. 배열/CDO나 피해 판단 로직은 변경하지
  않았고 생성 헤더도 직접 편집하지 않았다. 후속
  `Saved/Logs/AuthenticationClockBuild-Verified-20261008.log`는 성공했다.
  공유 변경이 포함된 Editor 증분 빌드이며 클린 빌드/Shipping/경고 0건 보장은 아니다.
- 최초 던전 자동화 `Automation-92e49d3dc4d641458f62ac5039904250`는 다른 UE 실행과
  겹친 에디터 초기화 중 180초 제한으로 종료됐다. 테스트 성공/실패 마커는 0개이며
  통과 근거가 아니다. 시작 대기 상한을 600초로 지정해 순차 재실행했다.
- 던전/인증 13개 성공:
  `Saved/Tests/DungeonFlow/Automation-4716fe31b6114e789c1137a5b2fd07d1/automation.log`.
  보상 스냅샷 3개 성공:
  `Saved/Tests/RewardSnapshot/Automation-cb6db0790863421596d3cceedcfe9814/automation.log`.
  보안 38개 성공:
  `Saved/Tests/Security/Automation-c54ec7e0251d4c7a87fba00fbd08660c/automation.log` 및
  `Security.proof.json`. 이 세 묶음은 총 54개이며 실패/Fatal/Assertion/Ensure 0건이다.
  보안 회귀에는 helper 변경을 사용하는 추가 피해 효과 경로 거절 검사가 포함된다.
- 인증 검증 당시 인접 보스 회귀는 전체 통과가 아니었다:
  `Saved/Tests/BossState/Automation-34c5ea64cae74969a2e0ea45ced77b87/automation.log`에서
  당시 발견된 12개 중 10개 성공, `Damage`/`RaidContent` 2개 실패였다.
  `Damage`는 비정상 능력치/0으로 반올림된 피해에도 GE 적용 성공을 기대하는 9개 검사,
  `RaidContent`는 저장된 공격의 실제 체력 감소를 기대하는 4개 검사에서 실패했다.
  서버 피해 receipt의 거절 계약과 테스트/저장된 콘텐츠를 함께 점검할 필요가 있었다.
  이번 인증 작업에서 해당 테스트·콘텐츠·피해 보안 규칙은 완화/수정하지 않았으며,
  이전 날짜의 보스 회귀 성공을 당시 바이너리의 통과 근거로 사용하지 않는다.
  2026-10-09 후속 최신 DLL 재검증에서는 보스 12개를 포함한 자동화 66개와 전용/리슨
  네이티브·실제 맵 네트워크 실행을 통과했다. 이미 반영된 공유 호환 수정, 보강한 회귀,
  새 해시/로그와 범위는 [BOSS_DAMAGE_EFFECT_CONTRACT.md](BOSS_DAMAGE_EFFECT_CONTRACT.md)에
  기록한다. 아래 인증 E2E는 당시 DLL의 기록으로 유지하며 새 DLL의 전체 인증 E2E로
  재분류하지 않는다.
- 실제 UE 클라이언트 2인·4인의 새 인증 계약/로비·결과 재로그인 E2E 성공:
  `Saved/Tests/DungeonBossReturn/1f780a547b184cd693fb2c78a771dc65/result.json`,
  `Saved/Tests/DungeonBossReturn/fc3b98761ddc4df280249f8410df0a06/result.json`.
  다음 옵션으로 실제 캐릭터 생성부터 로비 만료/재로그인 → 인증된 Iris 입장 → 보상 전
  `InProgress` 만료/401 → 서버 GAS 처치/정산 → 결과 재로그인/최신 완료 조회 → 실제
  복귀 버튼/로비 맵까지 검증했다. 각 캐릭터는 골드 500·인벤토리 수량 4·우편 0이며
  두 만료/재인증 검증 플래그가 true다. 테스트가 OS 시각이나 UE private 토큰을 바꾸지 않는다.

  ```powershell
  & .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 2 -LobbyUI -CreateCharactersInUI -ReauthenticateInLobby -ReauthenticateAfterAdmission -SkipPostgresStart -NoBackendRestore -TimeoutSeconds 600
  & .\Scripts\Network\Run-DungeonBossReturnE2ETest.ps1 -ClientCount 4 -LobbyUI -CreateCharactersInUI -ReauthenticateInLobby -ReauthenticateAfterAdmission -SkipPostgresStart -NoBackendRestore -TimeoutSeconds 600
  ```

  이는 새 응답 메타데이터가 실제 manager/로그인 UI에서 작동함을 보이는 개발 인증
  회귀다. 시계 오차/역행 자체는 앞의 격리/transport 테스트로 검증했으며, 실제 Steam·
  자연 협동 전투·60분 경과나 물리 PC 시각 변경으로 확대 해석하지 않는다.
- 기존 Entry 입장 경로 2인 회귀도 새 백엔드/Editor 모듈에서 성공했다:
  `Saved/Tests/DungeonBossReturn/0f3f38e55a934937b9da507fb4625e86/result.json`.
  만료/재로그인 플래그는 false이며 실제 입장·서버 GAS 처치·DB 보상·결과 버튼·로비
  복귀 및 각 캐릭터의 골드 500·인벤토리 4·우편 0을 확인했다.
- 세 E2E의 모든 서버/클라이언트 로그를 별도로 재검사했다. 위 두 재인증 실행은 각
  클라이언트의 HTTP 401 두 번·로비/결과 재로그인·인증 입장·실제 복귀 마커를 정확히
  한 번씩 확인했다. 런타임/네트워크 Error, Fatal/Assertion/Ensure, fixture 실패 및
  입장 우회는 0건이다. 이번 HTTP/E2E 소유 프로세스 0개, 3027/3031 TCP 및 18827 UDP
  리스너 0개를 확인했다. 임시 스키마 0개, 기존 개발 DB 컨테이너는 healthy로 보존했다.
  검증한 Editor 모듈 SHA256은
  `69DAEED2AA105E2CD1F787AF0EDFF2568ED585D6B197860B8B579CE493A2F1F4`이며,
  보안 회귀 전후 및 세 E2E 종료 후 동일함을 확인했다. 이 E2E 성공이 저장된 레이드
  공격 콘텐츠/모든 보스 자동화의 성공을 대신하지는 않는다.
