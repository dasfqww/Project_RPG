# 던전 연결 실패와 로컬 비상 복귀

2026-10-09. 정상 완료 복귀, 로컬 연결 종료, 백엔드 던전 포기는 서로 다른 동작이다.

## 구현 계약

- 정상 `ReturnToLobby`는 기존대로 인증된 최신 HTTP 응답의 동일 세션·캐릭터·멤버십과
  `Cleared`/`Failed`를 확인한다. 캐시나 로컬 승리만으로 정산 완료를 승인하지 않는다.
- 새 `DisconnectToLobby`는 현재 GameInstance의 로컬 클라이언트/단독 컨트롤러만
  사용한다. 현재 선택 캐릭터가 포함된 세션과 존재하는 `/Game/` 로비 맵이 필요하다.
  URL 옵션·원격 목적지·없는 맵을 허용하지 않으며 리슨 호스트/전용 서버를 이동시키지 않는다.
- 자신의 게임 클라이언트 NetDriver 실패 또는 `Connecting` 중 이동 실패는 같은 로컬
  복구를 예약한다. 다른 GameInstance, 서버/호스트 역할, 다른 종류의 NetDriver 실패는
  게임 클라이언트 연결 종료로 취급하지 않는다. 에러 문자열/티켓은 UI로 전달하지 않는다.
- UE의 기본 실패 처리도 `?closed`/기본 맵 이동을 예약하므로 로비 이동은 다음 core tick에
  예약한다. 로컬 절대 이동은 이전 서버 주소와 입장 URL 옵션을 가져오지 않는다.
  컨트롤러가 사라져도 자신의 GameInstance/world를 사용하는 자동 복구 경로를 유지한다.
- `ReturningToLobby` 동안 중복 입력·초기화·캐릭터 변경·다른 HTTP 작업을 잠근다.
  실제 로비 post-load에만 가드를 해제하고, 로비 이동 실패는 안전한 오류 코드로 끝낸다.
  아직 이동하지 않은 동안 원래 연결의 중복 진단이 복구를 취소하지 않도록 구분한다.
- 계정 바인딩/토큰·선택 캐릭터·현재 세션·멤버십·서버 정보는 보존한다. `InProgress`와
  `SettlementPending`은 로비에서도 활성 세션으로 남는다. 로컬 복귀 자체로 다음 던전이나
  다른 캐릭터 사용 권한을 부여하지 않으며, 새 입장은 새 백엔드 티켓을 요구한다.

## 늦은 HTTP 응답

`UHttpWebManager`의 클라이언트 던전 조회/변경 및 입장 티켓 요청은 제출 당시의 로컬
요청 세대를 캡처한다. 비상 복귀는 세대를 갱신하여 이전 응답이 새로운 로비 요청이나
재접속을 완료시키거나 같은 토큰의 오래된 401로 인증 상태를 바꾸지 못하게 한다.
새 세대의 실제 401/만료 검사는 그대로다. 서버 heartbeat/정산 응답은 이 클라이언트
무효화의 대상이 아니다.

이것은 HTTP 서버 작업의 취소/롤백이 아니다. 이미 제출한 변경은 백엔드에서 완료될 수
있으므로 로비에서 최신 상태를 다시 읽어야 한다. 인벤토리·보상 지급·세션 종료 API를
호출하지 않고 서버 전투/정산도 임의로 종료하지 않는다.

## UI

동일 계정·캐릭터·세션에 묶인 결과 화면에서 인증 만료 또는 요청 오류가 있고 다른
요청/로그인/이동이 진행 중이 아니면 `DisconnectToLobbyButton`을 사용할 수 있다.
버튼은 연결만 종료하고 세션은 유지하며 보상은 아직 확인되지 않았다고 표시한다. 정상 복귀·새로고침·
같은 계정 재인증 버튼은 기존 정책을 유지한다. 로비는 복구 이유와 상태 조회/재접속
안내를 표시한다. 이 버튼은 던전 포기나 보상 포기 버튼이 아니다.

## 입장 거절 후 Connecting 해제 (2026-10-16, native 완료·에디터 반영 대기)

- 아이템 표시 UI 검증에서 서버의 `invalid_join_ticket` 거절 후 `Connecting`이
  유지된 기록을 확인했다. UE 5.8의 `TickWorldTravel`은 입장 전 실패를
  `World == nullptr`로 알리므로, 기존의 null-world 조기 반환이 복구를 누락했다.
- 현재 `PendingNetGame`에 등록된 정확한 NetDriver와 소유 GameInstance로 실패 월드를
  찾는다. 이름만 같은 오래된 driver, 다른 인스턴스/월드, 서버 역할과 접속 완료 뒤의
  pending 진단은 처리하지 않는다. 기존 GameNetDriver 연결 실패 경로는 유지한다.
- 이미 로비에 있는 접속 실패는 자체 로비 이동을 추가하지 않고 안전한 오류 코드로
  잠금을 해제한다. PIE 접두어는 로비 비교에서 제거하며, 복구 목적지가 유효하지 않아도
  `Connecting`에 머무르지 않는다. 엔진 자체의 기본 disconnect 처리는 변경하지 않는다.
  세션·서버 정보·선택 캐릭터·멤버십과 포기 미확정 표시는 보존한다.
- `Run-DungeonClientFlowAutomationTest.ps1 -ConnectionRecoveryOnly`는 신규
  `DungeonPendingConnectionRecovery`와 직접 영향받는 기존 `DungeonReturn`/
  `DungeonRecovery`만 선택한다. 중복 모드를 실행 전에 거절하고, 독립 UserDir와 MCP
  자동 시작 차단으로 기존 에디터를 보존한다. 구현/테스트 TU 개별 컴파일·별도 모듈 링크,
  같은 DLL의 **3/3 native 통과·러너 exit 0**, 선택 입력과 닫힌 로그 해시를 확인했다.
- 증거: `Saved/Tests/PendingConnectionRecovery/eebb21d6e170404481d80e84d2c26a1a/`의
  `Recovery.proof.json`, `Inputs.json`, `Build/`와 별도 프로젝트의
  `Automation-4983a411e223401fb0c1e4a39d2e6f37/DungeonFlow.proof.json`/`automation.log`다.
  검증 DLL은 `47BC8885…`이며 실행 중인 공유 DLL `B11ADAC5…`/에디터·서버·백엔드는
  교체/종료하지 않았다. 별도 프로젝트에는 junction이 있으므로 재귀 삭제하지 않는다.
- 실패 callback과 월드 정보는 native fixture다. 실제 소켓/HTTP/맵 로드·수정 후 실제
  입장 거절 복구·전체 Editor 타깃·Steam/PostgreSQL/Shipping 통과가 아니다. 다음 정상
  빌드/재실행에 반영한 뒤 실제 거절 시 상태 해제와 로비 복구를 확인한다.

## 실제 입장 거절의 pending 실패·로비 로드 (2026-10-17)

- 재개 시 MCP `8000` 연결과 GUI가 로드한 공유 DLL `95D55E1F…`을 확인했다.
  해당 DLL의 기존 연결 회귀 5개와 선택 입력 13개의 불변/닫힌 로그를 확인하고 재실행하지 않았다.
- 기존 `RPGDungeonRecoveryTestSubsystem`과 네트워크 러너에
  `-PendingAdmissionOnly`를 추가했다. `-nosteam` 개발 fixture에서만 loopback 포트로
  티켓 없이 연결하고 실제 `ARPGGameModeBase::PreLoginAsync`의 `missing_join_ticket`
  거절을 사용한다. 콜백을 직접 호출하거나 서버의 티켓 검증을 완화하지 않는다.
  오프라인 세션/멤버십과 `Connecting` 시작 상태는 명시적 fixture다.
- 별도 서버/클라이언트에서 거절 1회 → 실제 `World == nullptr`의 소유 pending driver
  실패 이벤트 → `/Game/Maps/LobbyMap` 로드 → `Idle`/안전한 오류/입력 잠금 해제를 확인했다.
  세션 ID·상태·서버·선택 캐릭터·멤버십은 유지되고 URL의 서버/티켓 옵션은 제거됐다.
  예상 밖 네트워크 오류 0건, 러너 exit 0이다. 이미 로비에 있는 경우의 재로드/입력은
  이번 실통합 범위가 아니며 기존 native 검증과 구분한다.
- 개발 probe TU 하나만 컴파일하고 별도 DLL `A951B9C7…`로 링크했다. 최초 probe 로그
  분기의 컴파일 오류와 첫 실행의 DDC 경로 길이 초과를 보존했다. 로그 분기를 보완하고
  새 모드만 메모리 캐시를 사용하며 기존 연결 후 timeout 모드는 유지한다. 운영 게임 코드·
  BP·설정·백엔드/DB 변경이나 공유 DLL 교체·전체 빌드/기존 suite 반복은 없었다.
- 증거: `Saved/Tests/PendingConnectionNetwork/05da9fabac66425089013f128c432b94/`의
  `PendingNetwork.proof.json`, `Inputs.json`/`RetryInputs.json`, `Build/`/`RetryBuild/`,
  별도 프로젝트의 `DungeonRecovery/381305c771de49b9a41f4de432253a10/`에 있는
  `result.json`과 닫힌 서버/클라이언트 로그다. 선택 입력 7개·고정 artifact 15개 해시를
  확인했다. 자신의 프로세스 2개와 UDP `18842`는 정리했고 GUI 프로세스는 유지했다.
  본 작업은 GUI의 StopPIE를 호출하지 않았다. 별도 프로젝트의 junction은 재귀 삭제하지 않는다.
- 실제 소켓과 실패/로비 로드 검증이지만 인증 HTTP/유효 티켓 소비/운영 멤버십·
  Steam/PostgreSQL/Shipping·화면 품질 검증은 아니다. 원래 `invalid_join_ticket`의
  인증 경로 전체나 모든 실패 종류의 완료로 확대하지 않는다.

## 검증 경로

- `ProjectRPG.Online.DungeonRecovery`: 격리 월드에서 운영 복구 API/예약을 실행한다.
  미완료/정산 대기/완료 세션 보존, 중복 입력, 중단된 조회 응답, 잘못된 목적지·멤버십·
  컨트롤러, 실패한 입장, 중복 연결 진단, post-load 및 로비 이동 실패를 검사한다.
  실제 HTTP·백엔드 변경이나 맵 로드는 수행하지 않는다.
- 기존 `DungeonAuthenticationTransport`는 이전 세대의 세션/티켓 401 무시, 현재 세대
  401 적용과 계정 바인딩 보존을 추가 검사한다. 실제 토큰을 로그에 출력하지 않는다.
- 기존 결과 정책/위젯 회귀는 미인증 로컬 종료, 잘못된 계정·멤버십 차단, 요청 잠금과
  버튼 바인딩·해제·재생성을 검사한다.
- `Run-DungeonRecoveryE2ETest.ps1`: 개발 Editor의 실제 전용 서버와 원격 두 클라이언트를
  연결한 뒤 이 실행이 만든 서버만 종료한다. 실제 UE 연결 실패 처리와 로비 맵 로드,
  선택·세션·멤버십·활성 상태 및 URL 옵션 정리를 검사한다. 멤버십은 명시적인 오프라인
  fixture이며 인증되지 않았다. 백엔드/Steam을 호출하지 않으며 Shipping probe는 비활성이다.

```powershell
& .\Scripts\Tests\Run-DungeonClientFlowAutomationTest.ps1 -TimeoutSeconds 600 -SkipSDKSetup
& .\Scripts\Network\Run-DungeonRecoveryE2ETest.ps1 -TimeoutSeconds 600 -SkipSDKSetup
```

실행 전 계획이나 완료 마커만으로 통과를 표시하지 않는다. NullRHI는 GPU 화면 품질을
증명하지 않는다.

## 검증 기록 (2026-10-09)

검증한 Development Editor DLL SHA-256:

```text
276D16CB0B5FD6D1353DFF257331B4C303CA4162851E833F27F86D74E7F44846
```

- 최초 `Saved/Logs/DungeonRecoveryBuild-20261009.log`는 작업 재개 전에 중단돼 최종
  성공 기록이 없다. 이 로그만으로 전체 빌드 성공으로 처리하지 않았다. 공유 작업공간의
  후속 `Saved/Logs/RaidWeaponSweepFinalBuild-20261009.log`에서 DLL 링크와 빌드 성공,
  `Saved/Logs/SkillTargetDataAbuseBuild-20261009-Resume.log`에서 최신 타깃 확인 성공을
  확인한 뒤 위 해시의 바이너리로 검증했다. 다른 작업의 변경은 보존했다.
  기존 코드/플러그인 경고는 남으며 클린 빌드·Shipping·경고 0건을 뜻하지 않는다.
- 던전/복구/결과/로비/인증/보상 정의 14개 모두 성공:
  `Saved/Tests/DungeonFlow/Automation-ceeed25a12b74f2da75a6ecc7f791f2d/automation.log`,
  같은 경로의 `DungeonFlow.proof.json`. 필수 14개와 발견된 모든 던전 결과를 검사하고
  `Fail`뿐 아니라 `Skipped` 등 모든 비성공 결과를 거절한다. 실행 전후 해시도 일치한다.
- 같은 해시의 기존 보스 회귀 13개 모두 성공한 공유 결과를 확인했다:
  `Saved/Tests/BossState/Automation-d7f344268c8e484ca407135c1485b8e5/automation.log`,
  같은 경로의 `Boss.proof.json`. 이 단계에서 보스 기능을 새로 구현한 것으로 계산하지 않는다.
- 보안 38개 모두 성공:
  `Saved/Tests/Security/Automation-1df77617ddd248ca904527e36e5047b0/automation.log`,
  같은 경로의 `Security.proof.json`. 보상 스냅샷 3개도 모두 성공:
  `Saved/Tests/RewardSnapshot/Automation-ed06c94627bb4b69ad679d0d40c1fc9f/automation.log`.
  두 실행을 감싼 배치의 전후 DLL 해시가 동일하다. 14+13+38+3 총 68개 결과, 각 로그의
  비성공/Fatal/Assertion/Ensure 0건과 proof 해시를 별도로 확인한 통합 기록:
  `Saved/Tests/DungeonRecovery/5ce9589589a0476f91569560066d45cd/RecoveryRegression.proof.json`.
- 실제 전용 서버 종료 → 원격 두 클라이언트의 실제 로비 로드 성공:
  `Saved/Tests/DungeonRecovery/5ce9589589a0476f91569560066d45cd/result.json`.
  세 프로세스의 Iris, 각 클라이언트의 접속/로비 완료, 세션·멤버십·캐릭터·활성 상태,
  이전 서버 주소/Join Ticket 제거와 입력 가드 해제를 확인했다. 완료 후 2초 추가 검사와
  동일 DLL 해시 검사를 통과했고 테스트 소유 프로세스와 18841/UDP 리스너는 종료됐다.
- 위 통합 실행에는 의도적인 서버 종료 때문에 클라이언트당 `ConnectionTimeout`
  네트워크 Error가 정확히 1회씩 있다. 서버 종료 이후, 해당 실행의 클라이언트 로그,
  정확한 원격 포트·GameNetDriver·클라이언트 역할의 UE timeout 진단만 허용한다.
  중복/다른 네트워크 Error 및 fixture 실패/Fatal/Assertion/Ensure는 계속 거절한다.
  예상 timeout 2건, 예상 밖 네트워크 Error·fixture 실패/Fatal/Assertion/Ensure 0건이다.
- 최초 통합 실행 `Saved/Tests/DungeonRecovery/5b4340375cfc426ba3574f445860e8f4/`는
  스크립트가 의도적인 UE timeout Error를 일반 실패로 분류해 로비 로드 도중 중단됐다.
  성공 결과로 계산하지 않고 로그를 보존했다. 스크립트의 실제 판정 함수를 이전 로그로
  재검사해 종료 후 정확한 timeout 허용, 종료 전/미소유 로그 거절을 확인했고, 다른 포트·
  서버 역할·드라이버·실패 종류의 네 가지 변형도 거절했다. 수정 후 위 새 실행을 완료했다.

두 PowerShell 하네스의 구문 분석도 통과했다. 통합 참가 정보는 미인증 오프라인 fixture이며,
DB/Steam을 호출하지 않았다. 인증/계정 보존과 늦은 HTTP 응답은 격리 자동화 범위다.

## 남은 정책과 범위

후속 [DUNGEON_ABANDONMENT.md](DUNGEON_ABANDONMENT.md)에서 백엔드의 명시적
포기/보상 제외·전원 포기·정산/입장 경합과 영수증 재시도를 구현/검증했다.
2026-10-10에는 개발용 로컬 로비의 별도 포기 확인 UI·영수증 검사·같은 요청 재확인을 추가했다.
기본 실행/Shipping/게임 중 포기는 닫혀 있으며 게임 서버 퇴장/재검증 연결 및
로그아웃/계정 전환은 아직 구현하지 않았다. 검증 범위와 배포 제한은 포기 문서를 따른다.
현재 코드는 연결 단절을 자동 포기로 처리하지 않는다. 실제 Steam·인증된 플레이어·DB 정산과
단절/복귀의 연속 E2E, 운영 서버 장애·재접속·지연/손실, Shipping과 GPU 검증도 별도다.

## 예기치 않은 클라이언트 종료의 10분 유예 (정책 확정, 단계적 구현 중)

2026-10-25 표시 후속: 기존 서버 전용 멤버십 GET의 선택 표시 정보와 수신자 전용 파티 HUD를
연결했다. 단절 행 유지·서버 기준의 참고용 남은 시간·이전 세대 무시·불변 기록 후 행 제거이며
유예/보상 정책을 새로 구현하거나 화면 타이머로 자동 탈퇴를 승인하는 변경은 아니다.
구현 및 링크/실행 단계: [DUNGEON_PARTY_COMBAT_HUD.md](DUNGEON_PARTY_COMBAT_HUD.md#멤버십-명단-유지와-재접속-표시-2026-10-25-구현링크실행-대기).

게임 클라이언트가 갑자기 종료되거나 예기치 않게 서버 연결이 끊긴 경우를 대상으로 한다.
포기 버튼, 정상 완료 복귀, 의도적인 로컬 비상 복귀 버튼은 이 자동 탈퇴의 시작 신호로
간주하지 않는다. 서버가 관측한 연결 단절만 신뢰하며, 클라이언트의 로컬 시계나 단순
HTTP 실패·인증 만료·일시적인 멤버십 조회 실패만으로 보상 자격을 잃게 하지 않는다.
`Logout`만으로 계획된 종료와 예기치 않은 단절을 구분할 수 없으므로, 구현 전에
계획된 복귀 신호와 기존 연결의 세대/소유 범위를 분리해야 한다.

- 단절 시작 시각부터 **10분 미만**에는 같은 계정·캐릭터·던전 세션의 새 인증/입장
  티켓으로 재접속할 수 있다. 성공한 재접속만 타이머를 취소한다. 실패한 재시도나
  중복 단절 통지는 유예 시작 시각을 뒤로 밀지 않는다.
- 서버 기준 `now >= disconnectedAt + 10분`이면 그 캐릭터만 파티/공격대에서 자동
  탈퇴시키고 해당 인스턴스의 미정산 보상 대상에서 제외한다. 같은 인스턴스에 다시
  참가해 보상 자격을 되찾지 못하게 한다. 남은 참가자의 접속·전투·보상 권한은 유지한다.
- 정산 수령인 확정은 자동 탈퇴 판정과 같은 세션 잠금 아래 처리한다. 타이머 처리 작업이
  지연돼도 정산 시각에 이미 10분이 지난 멤버는 포함하지 않는다. 정산이 만료보다
  먼저 확정됐다면 기존 불변 수령인 목록을 소급 변경하거나 지급된 보상을 회수하지 않는다.
- 자동 탈퇴는 플레이어의 명시적 포기 영수증과 구별되는 서버 권위 기록이다. 게임 서버
  전체 장애를 개별 클라이언트의 10분 방치로 오인하지 않아야 한다.

현재 구현의 90초 `ActiveLeaseSeconds`는 서버 heartbeat가 모든 멤버에게 갱신하는
세션 lease이므로 위 10분 유예가 아니다. 전체 UE 단절→자동 탈퇴 흐름은 아직 연결되지
않았으며, 정산 시 만료자의 수령인 제외는 아래 2026-10-17 후속 단계에서 구현했다.
구현 검증에는 10분 직전/정각 재접속, 중복·늦은 단절 통지, 정산 동시 경합,
2~4인 남은 파티 보상, 서버/백엔드 재시작을 포함한다.

## 최초 단절 시각의 저장소 기반 (2026-10-16)

`IGameRepository.RecordDungeonMemberDisconnectionAsync`와 메모리/PostgreSQL 구현을
추가했다. 배정 서버 ID·세션·Steam ID·캐릭터 ID와 `Loading`/`InProgress`를 확인한 뒤
멤버의 `disconnected_at`이 비어 있을 때만 백엔드가 전달받은 UTC 시각을 저장한다.
같은 연결 단절의 중복 통지는 최초 시각을 반환하고 다른 멤버·세션 lease를 바꾸지 않는다.
기존 PostgreSQL 행은 스키마 초기화 시 nullable 열만 추가하며 과거 단절을 추정하지 않는다.

이 메서드는 **저장소 내부 기반이며 아직 HTTP/UE 서버에 연결하지 않았다.** 현재는
재접속 시 시각 취소, 10분 만료 자동 탈퇴·재입장 차단, 정산 수령인 제외를 하지 않는다.
특히 기존 연결에서 늦게 도착한 단절 통지가 새 접속을 뒤집지 못하도록 입장 세대를
묶고, 계획된 복귀와 예기치 않은 연결 종료를 구분한 뒤 서버 API를 열어야 한다.

백엔드 빌드 경고/오류 0, 메모리·PostgreSQL 포기/멤버십 회귀 전체 종료 0을 확인했다.
새 검사는 최초 시각 보존·다른 멤버/lease 무변경·잘못된 서버/신원 거절·새 PostgreSQL
연결의 시각 복구를 포함한다. 별도 스키마 초기화/기존 schema 업그레이드 회귀 10개도
통과했고 테스트 전용 private schema는 정리됐다. 실제 UE 연결 단절·10분 경과·보상
제외 E2E 증거는 아니다.

## 입장 세대와 늦은 단절 통지의 저장소 경계 (2026-10-16)

일회용 Join Ticket을 배정 서버가 성공적으로 소비하면 같은 세션·캐릭터 멤버 행의
`connection_generation`을 같은 트랜잭션에서 1씩 증가시키고 소비 응답에 반환한다.
`RecordDungeonMemberDisconnectionAsync`는 서버 ID·Steam ID·캐릭터 ID와 함께
**현재 세대**가 일치하고 0보다 클 때만 최초 `disconnected_at`을 기록/재확인한다.
이전 연결의 늦은 통지나 다른 멤버의 통지는 거절된다. 티켓 소비는 실제 UE
`InitNewPlayer` 완료가 아니므로 이미 있는 단절 시각을 지우지 않는다.

메모리·PostgreSQL에서 첫/재발급 티켓의 세대 증가, 중복 단절 시각 고정,
이전 세대 통지 거절, 다른 파티원/lease 유지와 새 DB 연결의 세대 복구를 검증했다.
백엔드 빌드 및 포기/멤버십 회귀 종료 0, 스키마 초기화/업그레이드 회귀 10개 종료 0이다.
기존 DB에는 `connection_generation BIGINT NOT NULL DEFAULT 0`을 추가하고 과거
접속 세대를 추정하지 않는다. 이 단계에서는 UE가 추가 필드를 읽지 않았다.
다음은 **실제 입장 확인 후** 유예 취소와 서버의 연결 세대 보유,
계획된 복귀/예기치 않은 `Logout` 구분, 10분 만료·정산 동시 판정이다. 입장 실패
또는 응답 유실 시 세대 예약 상태의 회복도 별도로 검증해야 한다.

## 실제 입장 확인의 백엔드 경계 (2026-10-16)

배정된 세션의 전용 게임 서버 토큰만 `POST /api/dungeon-sessions/{sessionId}/members/
{characterId}/admission-confirmation`을 호출할 수 있다. 요청의 `steamId`와
`connectionGeneration`을 현재 멤버·현재 세대와 대조하고, 백엔드 시각이 최초
`disconnected_at + 10분` **이전**일 때만 시각을 지운다. 10분 정각부터는 만료 작업이
아직 실행되지 않았더라도 409로 거절한다. 부모 세션 행 잠금 아래에서 판단하므로
단절 기록·티켓 소비·포기·정산의 저장소 경계와 직렬화된다.

`confirmed_connection_generation`은 확인된 접속 세대를 보존한다. 살아 있는 같은
연결의 확인 재시도는 멱등 성공이지만, 그 연결이 나중에 끊긴 뒤 도착한 오래된
확인 재전송은 단절 시각을 지우지 않고 거절한다. 다른 멤버와 90초 세션 lease는
변경하지 않는다. 기존 DB 행에는 이 세대를 0으로 추가하며 이전 접속을 확인된
것으로 추정하지 않는다.

메모리·PostgreSQL에서 10분 직전/정각, 세대·신원·서버 거절, 중복 확인과 후속
단절 보존, 실제 HTTP 서버 토큰 경계, 새 저장소 연결에서의 영속 복구를 확인했다.
스키마 신규 생성/기존 멤버 열 업그레이드 11개도 통과했다. 이 단계에서는 **UE 서버가
`connectionGeneration`을 읽거나 실제 입장 완료 뒤 이 API를 호출하지 않았다.**
예기치 않은 `Logout` 신호, 10분 자동 탈퇴·정산 제외·재입장 차단은 미구현이다.

## UE 티켓 소비 세대의 검증·보관 (2026-10-16)

전용 서버의 티켓 소비 HTTP 200 본문에서 `connectionGeneration`을 숫자 원문으로
읽고 양의 `int64` 정수일 때만 승인한다. 누락·문자열·소수·지수 표기·0·음수·범위
초과는 실패로 처리해 `PreLoginAsync`를 통과하지 못하게 한다. 검증된 세대는
기존 세션·Steam ID 대조를 거친 `FValidatedAdmission`을 통해 `InitNewPlayer`의
서버 `PlayerState`에만 보관하고, 인증 신원 회수 시 함께 지운다. 티켓 응답만으로
백엔드 `disconnected_at`은 지우지 않는다.

신규 코덱 테스트는 정상/큰 정수와 잘못된 타입·범위를, 기존 신원 회수 테스트는
세대 보관·회수를 검사하도록 추가했다. 변경된 구현·테스트 C++ 6개는 에디터가
열린 상태에서 Live Coding pass-through `-SingleFile -NoLink` 컴파일을 통과했다.
공유 DLL 링크·새 바이너리의 네이티브 실행·HTTP/UE 접속 E2E는 이번 검증에 포함하지
않았다. **다음은 `InitNewPlayer` 이후 비동기 확인 응답 전에는 플레이어를 활성화하지
않는 게이트와 입장 확인 API 호출·실패/응답 유실 처리다.** 예기치 않은 단절 보고,
10분 탈퇴·정산 제외도 여전히 미구현이다.

## UE 비동기 입장 확인 게이트 (2026-10-16)

`InitNewPlayer`의 티켓·세션·연결 신원 검증 뒤에는 인증 신원 설정과 인벤토리
조회부터 보류한다. UE 엔진은 이 함수가 끝난 뒤 컨트롤러에 네트워크 연결을 붙이고
`PostLogin`을 호출한다. 서버는 그 자리에서 정확한 컨트롤러·연결·세션·캐릭터·
Steam ID·접속 세대를 보관하고, 세션 전용 게임 서버 토큰으로 백엔드의
`admission-confirmation` POST를 보낸다. 수신된 **HTTP 200 + JSON true**만
승인하고 나서 `PlayerState` 신원·인벤토리 조회·기본 `PostLogin`을 진행한다.
대기 중의 `RestartPlayer`도 활성화 집합으로 막는다. 거절/HTTP 실패 시 신원을
열지 않고 동일 소유 연결만 닫으며, `Logout`·월드 종료는 대기 상태를 지워 늦은
콜백이 새 연결이나 다른 파티원을 활성화하지 못하게 한다. 로컬 E2E 프록시와
기존 명시적 개발 네트워크 우회는 원래 경계 그대로 둔다.

변경 UE 4개 구현·테스트 C++의 Live Coding pass-through 단일 파일 `-NoLink`
컴파일은 통과했다. 이는 공유 DLL 링크·네이티브 자동화 실행·실제 UE/HTTP/DB
재접속 E2E가 아니다. **특히 확인 커밋 후 200 응답만 유실되어 서버가 연결을 닫은
경우에는 백엔드의 단절 시각을 다시 기록할 서버 단절 보고 경계가 필요하다.**
계획된 복귀와 예기치 않은 `Logout` 구분, 10분 자동 탈퇴/보상 제외, 2~4인
네트워크 검증은 아직 남았다.

## 서버 전용 단절 보고 HTTP 경계 (2026-10-16)

`POST /api/dungeon-sessions/{sessionId}/members/{characterId}/disconnection`은
배정된 세션의 게임 서버 토큰만 받는다. 본문은 `steamId`와 양의
`connectionGeneration`이며, 저장소는 현재 세대·정확한 멤버/서버와 진행 중
세션을 같은 세션 잠금에서 대조한다. 최초 보고는 백엔드 UTC 시각을 저장하고
`200 {disconnectedAt}`으로 반환한다. 중복 보고도 처음 시각을 반환해 10분
기한을 연장하지 않는다. 오래된 세대/다른 멤버/서버는 403, 잘못된 본문은
400, 완료된 세션의 새 보고는 거절한다. 플레이어 토큰으로 호출할 수 없다.

메모리 저장소의 실제 HTTP 회귀에서 익명/플레이어/타 서버·신원·이전 세대
거절, 최초/중복 시각, 단절 뒤 입장 확인 재전송 거절과 정산 뒤 제한된 서버
토큰의 변경 거절을 확인했다. 포기/멤버십 총 34개 시나리오가 종료 0이다.
이번 실행에는 PostgreSQL 테스트 연결이 없어 HTTP+PostgreSQL 조합은
검증하지 않았다. 이 단계에서는 UE 게임 서버의 `Logout`이나 입장 확인 응답 유실 경로가
아직 이 API를 호출하지 않았다. 다음 단계는 계획된 복귀·명시적 포기와
실제 비정상 단절의 소유 연결/세대 분리 후 UE 보고를 연결하는 것이다.

## UE 입장 실패 소켓의 단절 보고 (2026-10-16)

입장 확인 요청의 HTTP 완료 콜백에서 확인 응답을 수락할 수 없거나, 백엔드
확인 뒤 로컬 `PlayerState`/연결 검증이 실패해 **동일 소유의 열린 소켓**을
닫을 때 서버 토큰으로 `disconnection` POST를 시작한다. 확인 커밋 뒤 200
응답만 유실된 경우 백엔드 시각을 다시 기록하기 위한 경로다. 정확한 세션·
캐릭터 GUID, Steam 숫자 ID, 양의 `int64` 세대를 공통 코덱으로 검증하고
정수 세대를 반올림하지 않고 JSON에 쓴다. 확정적인 HTTP 4xx 거절이나
명시적 포기 영수증에는 보고하지 않는다. 다른 연결/자식 연결 또는 월드
종료에서 임의 보고하지 않는다.

변경한 UE 구현·코덱 테스트 C++ 4개가 에디터를 유지한 채 `-NoLink` 개별
컴파일을 통과했다. 새 게임 DLL 링크/네이티브 테스트 실행/UE-HTTP-DB
응답 유실 E2E는 아직 없다. 이 단계의 보고는 best-effort 한 번이며 HTTP 실패
재시도·영속 큐가 없었다. 확인 응답 전에 소켓이 사라져 `Logout`이 대기
기록을 지우는 경우도 아직 보고하지 않는다. 이를 포함한 일반 비정상
단절과 계획된 복귀의 분리, 10분 자동 탈퇴/정산 제외는 다음 단계다.

## UE 단절 보고의 제한 재시도 (2026-10-16)

위 입장 실패 소켓의 보고 요청이 시작되지 않거나 전송 오류·5xx로 끝나면
같은 세션·캐릭터·Steam ID·접속 세대를 1초와 3초 뒤 최대 총 3회 시도한다.
백엔드의 최초 단절 시각 보존과 현재 세대 검사가 중복·늦은 요청을 제한한다.
200 수락 또는 4xx 권한/상태 거절에는 재시도하지 않는다. 서버 서브시스템
종료, 배정 세션 변경, 유효한 게임 월드 부재에서는 다음 요청을 시작하지 않는다.
재시도 간격/중단 조건을 코덱 자동화 테스트에 추가했고 변경 UE C++ 3개의
`-NoLink` 개별 컴파일을 확인했다. 공유 DLL 링크·새 바이너리 실행·실제
네트워크 장애/백엔드 응답 유실 E2E 증거는 아직 없다.

이 큐는 월드 타이머 기반의 **프로세스 내 제한 재시도**이며 영속 저장이
아니다. 서버가 재시작하거나 모든 요청이 실패하면 단절이 DB에 기록됐다고
보장하지 않는다. 최초 성공이 뒤로 밀리면 백엔드 기준 유예 시작도 늦어질
수 있다. 일반 예기치 않은 `Logout`과 계획된 복귀 구분, 확인 응답 전 소켓
종료, 10분 탈퇴·정산 자격 판정은 여전히 별도다.

## 입장 응답 전 소켓 정리의 순서 보장 (2026-10-17)

입장 확인 HTTP가 진행 중인데 해당 원격 연결이 먼저 정리되면 UE 엔진은
`OnNetCleanup`에서 컨트롤러의 일반 `Player` 포인터를 지운 뒤 `Logout`을
호출한다. 이제 서버는 컨트롤러의 정리 콜백 범위 내 연결과 대기 기록의 연결·컨트롤러·
PlayerState·월드/드라이버가 정확히 일치할 때 콜백 상태에만 단절을 표시한다.
대기 기록은 기존처럼 즉시 제거하므로 늦은 성공 응답이 신원/Pawn을 다시
활성화하지 않는다. HTTP 완료 콜백이 그 표시를 보면 같은 세대의 단절
보고를 시작한다. 다만 UE의 HTTP 타임아웃은 백엔드 트랜잭션 종료 증거가
아니므로 콜백 순서만으로 역전 경합을 해결했다고 볼 수 없다. 아래의
저장소 단절 세대 묘비가 보고 후 도착하는 늦은 확인도 차단한다. 명시적
4xx 거절·포기 영수증·월드 종료는 제외한다.

GameMode 변경 C++ 1개의 `-NoLink` 개별 컴파일만 확인했다. 공유 DLL 링크,
실제 `OnNetCleanup` 순서/HTTP 응답 유실/백엔드 상태의 E2E 검증은 아직
없다. 이 경로는 입장 활성화 **전** 대기 연결만 다룬다. 일반 활성 접속의
계획된 정상 복귀·비상 로비 버튼과 예기치 않은 `Logout` 구분, 서버 전체
장애 시 개별 단절 오인 방지, 10분 자동 탈퇴·정산 자격 판정은 별도다.

## 설치 엔진의 연결 정리 콜백 호환 (2026-10-17)

설치 엔진에서 export되지 않는 `UNetConnection::GNetConnectionBeingCleanedUp`의
직접 참조를 제거했다. `ARPGPlayerController::OnNetCleanup`에서 실제 연결을
`TGuardValue`로 기록하고 `Super`의 동기 `Logout`이 끝나면 즉시 복구한다.
GameMode는 이 연결과 기존 월드/드라이버/컨트롤러/PlayerState/세대 검사를 함께
사용한다. 엔진 수정·새 RPC·신원/재접속 정책 변경은 없다.

Editor 전체 빌드와 최종 DLL의 기존 `DungeonEjection.Lifecycle`,
`DungeonConnectionCleanup`, `DungeonPlannedDisconnect` 3종은 모두 통과했다.
증거는 `Saved/Build/RaidCleanupFix/EditorBuild-Assertion-Retry.log`와
`Saved/Tests/DungeonCleanupFix/33ae2433eb134cd4b04c42e2b36b6919/Cleanup.proof.json`이다.
최초 Lifecycle의 정리 후 weak 포인터 비교 실패는 객체 식별자 비교로 보완했고
실패 로그는 별도 실행 폴더에 보존했다. 실제 HTTP/소켓/Steam 재접속 E2E나
10분 자연 경과의 증거는 아니다.

## 단절 세대 묘비와 늦은 확인 차단 (2026-10-17)

멤버에 `disconnected_generation BIGINT NOT NULL DEFAULT 0`을 추가했다.
배정 서버의 현재 세대 단절 보고는 기존 `disconnected_at` 최초 시각을
유지하면서 이 세대를 현재 접속 세대로 기록한다. 같은 세대의 입장 확인이
나중에 잠금을 얻으면 409로 거절하고 시각을 지우지 않는다. 반대로 확인이
먼저 커밋되더라도 후속 단절 보고가 시각/세대를 기록하므로 최종 상태는
단절이다. 새 티켓이 세대를 증가시킨 경우에만, 최초 단절부터 10분
**미만**이고 그 새 세대가 아직 단절 보고되지 않았을 때 확인이 유예를
취소한다. 실패한 재시도는 최초 시각을 뒤로 밀지 않는다.

메모리 저장소의 보고 우선/동시 확인 경합, 연속 실패 재입장과 새 세대
복구를 포함한 포기 25개·멤버십 11개 시나리오가 통과했다. PostgreSQL
구현은 같은 부모 세션 행 잠금 아래 갱신/조회하며 기존 행에 0 열을 추가한다.
스키마 업그레이드 테스트를 작성하고 두 테스트 프로젝트의 빌드는 경고·
오류 0이었지만, 로컬 PostgreSQL 포트가 닫혀 있어 실제 DB 경합/업그레이드
실행은 하지 못했다. 기존 DB의 과거 단절 시각이 어느 세대에서 생겼는지는
새 열만으로 역추론하지 않으며 0으로 시작한다. 혼합 버전 배포와 실제
UE-HTTP-DB 응답 유실 E2E도 아직 검증해야 한다.

## 10분 유예 만료자의 정산 수령인 제외 (2026-10-17)

새 보상 정산을 만들 때 `disconnected_at`이 있고 서버 기준 경과 시간이 **10분 이상**인
멤버만 수령인에서 제외한다. 표식이 없는 멤버·미래 시각은 단절/만료로 추정하지 않는다.
10분 직전에는 수령인에 포함되고, 기한 내 새 세대의 성공한 입장 확인으로 표식이 지워졌다면
다시 포함된다. 티켓 소비만으로는 자격을 복구하지 않으며 중복 단절 보고도 기한을 연장하지 않는다.

메모리의 던전 gate/PostgreSQL의 부모 세션 행 잠금 안에서 수령인을 확정한다. 운영 호스트가
이미 등록한 `TimeProvider`를 두 저장소 생성자에 주입하고, **잠금 대기 후** 다시 읽은 서버 시각을
사용한다. 직접 생성한 기존 저장소 호출은 clock을 생략하면 명시적 `now` 계약을 유지한다.
같은 clock을 입장 확인에도 적용해, 10분 전에 시작한 요청이 잠금 대기 중 정각을 넘겼을 때
단절 표식을 지우고 보상 자격을 되찾는 경로도 막았다. 기존 단절 세대 묘비 검사는 유지한다.

이미 만들어진 정산은 재시도/새 저장소 연결에서도 원래 수령인·생성 시각을 반환한다. 이후
기한이 지나도 확정 수령인을 소급 제거하거나 지급된 보상을 회수하지 않는다. 모두 만료된
실제 파티는 수령인 0명의 정산을 만들고 기존 worker가 빈 배치로 완료할 수 있게 했다.
멤버/lease/티켓을 삭제하거나 명시적 포기 영수증을 대신 만들지 않는다. **자동 탈퇴와 재입장
차단의 전체 연결은 아직 별도**이며, 이번 변경은 정산 시점의 자격 판정이다. 스키마/HTTP/BP
변경이나 UE 빌드·실행은 없다. 진행 중이던 UE 단절 보고/재시도 구현도 수정하지 않았다.

검증:

- 최종 Release 빌드 경고/오류 0. 메모리·실제 PostgreSQL 각각 새 10개 시나리오와 정산/입장
  확인의 실제 잠금 대기 2종을 통과했다. PG는 새 연결에서도 확정 수령인의 영속성을 확인했다.
- 두 저장소의 기존 포기 25개·멤버십 11개 시나리오도 종료 0이다. 테스트가 생성한 private
  schema만 소유권 확인 뒤 정리했고 기존 public 데이터/볼륨·다른 프로세스는 유지했다.
- 새 입장 확인 잠금 회귀를 수정 전 backend DLL에 실행하면 `stale pre-lock admission clock`
  오류로 실패했다. 같은 새 테스트와 수정된 최종 DLL에서는 메모리/PG 모두 통과했다.
- 실제 worker의 수령인별 배치 생성과 0명 완료까지 확인했지만, 이 새 테스트의 커밋 대상은
  기록용 fixture다. 새 조건에서 실제 재화/아이템 지급·UE 화면·네트워크 전체 E2E를 완료했다고
  표시하지 않는다. 일반 예기치 않은 `Logout` 신호와 계획된 복귀 구분도 아직 남는다.
- 기한은 테스트용 clock을 경계 전후로 이동해 검사했다. 메모리 gate와 PG 행 잠금의 대기는
  실제로 발생했지만, 벽시계로 10분을 기다린 자연 경과/실제 UE 재접속 검증은 아니다.

증거: `Saved/Tests/ConnectionRewardEligibility-bb9b3cb52da9457798e069327129a519/`의
`FinalBuild.log`, `FinalRegressionTests.log`, `BeforeFixReproduction.log`, `FinalAttempt.proof.json`,
`Verification.proof.json`, `VerifiedInputs`와 `build-final`이다. 첫 수령인 필터 단계의 빌드/통과
로그와 수정 전 DLL·선택 입력은 `build`, `BeforeAdmissionClockFix`, `BeforeFixReproduction`에
분리해 보존하며 최종 검증과 혼합하지 않는다.

```powershell
# 새 정산 자격/잠금 경계만 선택. PostgreSQL은 기존 loopback 개발 DB를 지정한다.
dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests -c Release -- --connection-rewards-only --require-postgres
```

## 만료 후 Join Ticket 소비 차단 (2026-10-17)

티켓 소비는 입장 확인보다 앞서 입장 세대를 예약한다. 이제 메모리/PG 저장소 모두 던전
세션 잠금을 획득한 뒤 주입된 서버 시각으로 최초 단절 표식을 검사한다. `disconnected_at +
10분` 정각부터 소비를 거절하며, 거절된 티켓은 소모하거나 접속 세대를 올리지 않는다.
10분 직전에는 소비할 수 있지만, 실제 입장 확인이 정각 뒤에 도착하면 기존 확인 경계에서
거절된다. 기한 내 새 세대의 성공한 확인이 단절 표식을 지우면 이후 티켓은 정상 소비된다.
단절 표식 없는 다른 파티원은 영향받지 않는다. 기존 플레이어 티켓 발급 API 자체는 아직
단절 만료를 검사하지 않으므로 발급된 티켓이 소비 단계에서 거절될 수 있다.
이 발급 경계는 아래 2026-10-18 후속 구현에서 보완했다.

메모리/PG 각각 경계·복구 2개와 실제 gate/부모 행 잠금 대기 1개를 추가했다. Release
전체 회귀에서 각 저장소 보상 자격 12개·잠금 3개·기존 포기 25개·멤버십 11개가 종료 0이고,
PG 영속/행 잠금 회귀도 통과했다. 테스트의 private 스키마만 정리하고 public 데이터는
변경하지 않았다. 스키마/HTTP 응답 형식·UE는 변경하지 않았다. 이 경계만으로 멤버/lease가
자동 삭제되거나 서버 퇴장 기록이 생성되지는 않는다. 다음은 별도 불변 자동 탈퇴 기록과
서버 감지/주기 처리, 실제 UE 재접속 검증이다.

## 활성 접속의 계획된 복귀와 비정상 단절 분리 (2026-10-17)

입장 확인이 성공한 서버 연결의 세션·캐릭터·Steam ID·접속 세대와 실제
PlayerState/UNetConnection을 GameMode에 함께 보관한다. 일반 `Logout` 호출만으로는
보고하지 않는다. 엔진의 현재 `OnNetCleanup` 연결, 해당 컨트롤러/PlayerState 소유권,
현재 서버 월드/드라이버와 저장한 신원·세대가 일치하는 비계획 종료만 기존 서버 전용
단절 보고/제한 재시도로 보낸다. 명시적 포기 영수증·취소된 신원·다른 소켓/세대와
월드 종료/EndPlay/엔진 종료 중에는 새 보고를 만들지 않는다. 서버 전체 장애의 모든
형태를 검증한 것은 아니며 영속 보고 큐도 추가하지 않았다.

기존 BP 공개 진입점 `ReturnToLobby`/`DisconnectToLobby`는 유지한다. 명시적으로 이
API를 호출한 로컬 `RPGPlayerController`만 인자 없는 owning-actor reliable server RPC를
보낸다. 서버는 클라이언트가 고른 신원·시각·세대를 받지 않고, 활성화 때 저장한 현재
연결을 검증한 뒤 **계획 종료로 표시하는 즉시 그 소켓을 닫는다.** 표시만 하고 계속
플레이하는 면제를 만들지 않는다. 자동 네트워크 실패 복구에서는 이 RPC를 보내지 않는다.
정상 완료 복귀의 `Cleared`/`Failed` HTTP 재확인은 계속 필요하며 비상 복귀는 포기/정산이 아니다.

로컬 이동은 서버의 닫힘을 최대 2초간 core ticker로 기다린다. 서버가 응답하지 않아도
비상 로비 이동을 막지는 않는다. **RPC가 서버에 도달하지 못했다면 계획 종료로 분류됐다는
보장은 없으며**, 그 뒤 서버가 관측한 단절은 비정상으로 보고될 수 있다. 이 경우 유예를
취소하려면 기존 새 티켓/새 세대의 성공한 입장 확인이 필요하다. 대기 중 새 서버 연결로
교체되면 이전 복귀 요청이 새 연결을 로컬 이동으로 끊지 않도록 중단한다.

추가로 엔진 `APlayerController::OnNetCleanup`은 `Logout` 전에 `Player`/
`NetConnection`과 연결의 `PlayerController`를 지운다. 기존 입장 대기 단절 검사에서
`Connection->PlayerController == Controller`만 요구하던 누락 조건을 수정했다. 엔진
`RPGPlayerController`가 `Super::OnNetCleanup` 호출 동안만 보관하는 현재 정리 소켓과
저장된 정확한 소켓/PlayerState/소유자를 계속 요구한다. 다른 포인터로 교체된 경우에는
거절하므로 null 허용이 임의 신원 보고 권한이 되지는 않는다. 엔진 비공개 전역 변수의
DLL export에 의존하지 않으며, 정리 스택 밖에는 이 연결 표시를 유지하지 않는다.

네이티브 테스트에는 정확한 정리 소유권·다른 멤버/이전 세대/포기/월드 종료 제외,
실제 엔진의 입장 대기 CleanUp/Logout 순서, 계획 신호의 즉시 Close/정상 Cleanup,
남은 파티 보존, RPC의 신원 인자 부재, 로컬 대기/기한/연결 교체 경계를 추가했다.
변경 C++ 5개의 **`-NoLink` 개별 컴파일은 성공**했다. 최초 개별 컴파일에서 드러난
`UPackage`/`UGameInstance`/`UChildConnection`의 간접 include 의존을 명시적 include로
수정하고 같은 5개를 다시 통과했다. 기존 CommonUI 플러그인 의존/StructUtils 폐기
예고 경고는 남아 있다. 전체 재빌드로 커진 자체 시도는 중단했고, 그 성공을 주장하지 않는다.
다른 작업의 전체 UE 빌드가 공유 DLL을 사용하기 시작하여 게임 모듈의 별도 링크는
실행 전에 거절했다. **이 변경의 새 DLL 링크·네이티브 자동화 실행은 아직 미검증**이다.
다른 작업의 빌드 성공이나 이전 DLL 결과를 현재 변경의 런타임 통과로 대체하지 않는다.

증거: `Saved/Tests/DungeonConnectionLifecycle-a0f26c6e278c486581cdf1e5bb147e85/`의
`FocusedCompileFinal.log`, `BuildInputs.json`, `Verification.proof.json`, 선택 입력/객체
사본이다. 검증 상태는 `compiled_runtime_pending_shared_build`다. 기존 러너의
`-ConnectionRecoveryOnly`에 신규 2개를 포함한 5개 선택 테스트를 연결했지만 아직
실행하지 않았다. 다음은 공유 빌드 종료 후 현재 소스/DLL 일치 확인과 해당 5개·기존
`-EjectionOnly` 회귀 실행이다. 실제 소켓/RPC 전송·HTTP/DB·Steam/Shipping, 서버 전체
장애·영속 보고·10분 자동 탈퇴의 완료 증거와 계속 구분한다.

위 내용은 당시의 빌드 대기 기록이다. 이후 새 DLL의 네이티브 검증 결과는 아래
「계획 복귀·단절 분리의 새 DLL 네이티브 검증」을 따른다.

## 단절 유예 만료의 백엔드 퇴장 기록 (2026-10-17)

배정된 세션의 게임 서버 토큰만 `POST /api/dungeon-sessions/{sessionId}/disconnection-expirations`
를 호출한다. 본문 시각·캐릭터 지정은 받지 않는다. 백엔드는 메모리 dungeon gate 또는
PostgreSQL 부모 세션 행 잠금 뒤 서버 시각을 읽고, 활성 `Loading`/`InProgress` 세션의
`disconnected_at`이 10분 정각을 지난 멤버만 처리한다. 자연 세션 만료, 표식 없는 멤버,
기한 내 실제 입장 확인으로 복구한 멤버는 퇴장 기록을 만들지 않는다.

한 트랜잭션에서 만료자의 멤버십·그 세션의 캐릭터 lease/남은 티켓을 제거하고
`dungeon_session_disconnection_expulsions`에 세션/캐릭터/Steam ID, 최초 단절 시각,
정확한 만료 시각, 첫 처리 시각을 저장한다. 명시적 포기의 `abandonments`를 만들거나
보상·아이템을 회수하지 않는다. 나머지 파티의 세션/서버 할당은 유지하고 마지막 멤버면
`Closed`로 닫는다. 응답은 `disconnectionExpulsions` 배열이며 활성 세션 재호출에도 최초 기록을
반환한다. 마지막 퇴장으로 닫힌 후 게임 서버 자격은 기존 제한 모드로 바뀌어 POST는
금지되지만, 기본 5분의 post-session 조회 유예 안에서 같은 서버의 멤버십 스냅샷 GET으로
기록을 다시 읽을 수 있다. 이 조회에는 별도의 `disconnectionExpulsions` 배열을 추가했다.
당시 UE 스냅샷 코덱은 추가 필드를 무시했으며, 후속 네이티브 검증·캐시는 아래에 기록한다.

정산/입장 확인/티켓 소비와 같은 세션 잠금이므로 만료 처리 후 옛 티켓이나 재입장으로
자격을 되찾지 못한다. 정산이 먼저 수령인을 확정한 경우에는 그 목록을 소급 변경하지
않고 종료 상태에서 새 퇴장 기록도 만들지 않는다. 만료 처리가 먼저 마지막 멤버를
제거하면 세션이 닫혀 새 정산은 시작되지 않는다. 기록 INSERT가 실패하면 멤버십·lease·
티켓·세션 상태도 모두 롤백한다.

Release에서 메모리/PostgreSQL 각 6개 신규 시나리오(서버 전용 HTTP와 Closed 조회 포함),
실제 gate/부모 행 잠금 대기, PG의 기록 실패 롤백·재시도, 기존 포기 25개·멤버십 11개·
보상 자격 12개, 스키마 초기화/업그레이드 12개가 통과했다. 테스트 전용 private 스키마만
정리했고 public 데이터는 유지했다. **이 백엔드 단계에서 UE 게임 서버의 주기 호출,
새 기록의 네이티브 검증·실제 소켓 회수, 단절 보고의 재시작 내구성,
실제 지급/네트워크 E2E는 미완료**였다.
따라서 API 구현만으로 운영 자동 탈퇴 전체가 동작한다고 표시하지 않는다.

## UE 만료 퇴장 기록의 검증·캐시 (2026-10-17)

전용 서버의 기존 멤버십 GET 코덱에 선택적 `disconnectionExpulsions` 배열을 추가했다.
필드가 없는 구형 응답은 계속 읽되, 필드가 있으면 배열 타입·전체 128개 제한,
세션 내 멤버/명시적 포기/만료 퇴장 간 캐릭터 중복을 검사한다. 각 기록은 정확한
캐릭터 GUID·Steam ID, UTC `disconnectedAt`/`expiredAt`/`processedAt`, 최초 단절에서
만료까지 정확히 10분, 만료 이후의 첫 처리 시각, `rewardExcluded=true`가 필요하다.
잘못된 항목 하나라도 있으면 전체 조회를 실패로 처리하고 부분 근거를 남기지 않는다.

서버 모니터는 기존 명시적 포기와 만료 퇴장을 별도 사유로 유지한다. 정확한
세션/배정 서버/캐릭터/계정이 일치하는 불변 기록만 세션별로 누적하며, 이후의 빈 조회나
HTTP 오류로 이미 확인한 퇴장을 취소하지 않는다. 사유 충돌·다른 계정 재사용은 거절하고
월드/배정 변경 시 캐시를 지운다. 단순 멤버 부재나 단절 보고 자체는 퇴장 증거가 아니다.
코덱과 모니터의 정상/Closed·오류/불일치 경계에 네이티브 자동화 사례를 추가했다.

변경 C++ 3개 파일의 개별 `-NoLink` 컴파일에 이어 전체 `Project_RPGEditor Win64
Development` 타깃의 56개 작업·게임 DLL 링크가 성공했다. 새 DLL에서
`-MembershipEjectionOnly` 집중 자동화 6개(코덱·모니터·전송 및 기존 멤버십 회수
3개)가 모두 통과했다. 로그와 DLL/선택 소스 해시 proof는
`Saved/Tests/DungeonFlow/Automation-5544be87e38443c3910b4300808d5989/`에 있다.
이 검증은 네이티브 fixture이며 실제 백엔드 POST/GET·소켓·PostgreSQL·Steam E2E는 아니다.

**이번 단계에서는 만료 처리 POST를 주기 호출하지 않고, 캐시를 GameMode의 입장 차단·
실제 소켓 회수에 아직 연결하지 않았다.** 따라서 운영 자동 퇴장, 영속 단절 보고,
실제 지급/네트워크 E2E가 완료됐다는 뜻이 아니다. 다음은 서버 전용 POST/GET 순서와
닫힌 세션의 조회 유예를 다루고, 캐시된 동일 신원만 게임에서 제거·재입장 차단하는 경계다.

## UE 전용 서버의 만료 처리 POST·스냅샷 GET 순서 (2026-10-17)

배정된 던전의 UE 전용 서버가 기존 5초 멤버십 폴링마다 먼저
`POST /api/dungeon-sessions/{sessionId}/disconnection-expirations`를 본문 없이 요청한다.
서버 전용 bearer만 사용하고 일반 플레이어 인증으로 대체하지 않는다. 이 POST와 기존
스냅샷 GET은 하나의 in-flight 슬롯/세대 취소를 공유해 중복 요청이나 오래된 콜백이
새 세션의 조회를 가로채지 못하게 한다. POST 완료 뒤 같은 배정/월드인지 확인하고
GET을 보낸다. POST가 403(마지막 만료자로 닫힌 세션), 5xx 또는 전송 실패여도
조회 유예 안의 기존 불변 기록을 회수할 수 있도록 GET을 시도한다. 반대로 POST의
상태 코드·본문만으로는 캐릭터 권한을 변경하지 않는다. 유효한 GET 스냅샷만 기존
모니터의 퇴장 근거가 된다.

이 절의 변경은 POST/GET 순서와 전송 경계다. 공유 작업 공간에서 별도로 반영된
GameMode의 만료 기록 기반 입장 차단·소켓 회수와 함께, 전체 Editor 타깃
65개 작업·새 DLL 링크, 집중 네이티브 8개를 통과했다.
여기에는 전송·코덱·모니터와 별도 회수 경계의 테스트가 포함된다. 로그 및 DLL/소스
해시 proof는 `Saved/Tests/DungeonFlow/Automation-115fac82a7e4437eb3e3862edcd9d830/`에
있다. 실제 UE↔백엔드/PostgreSQL POST→GET→소켓 회수·2~4인 보상 E2E는 미검증이다.

## 계획 복귀·단절 분리의 새 DLL 네이티브 검증 (2026-10-17)

공유 `Project_RPGEditor Win64 Development` 타깃이 정상 완료된 뒤 그 DLL로 다음을
실행했다. 타깃 빌드는 다른 작업의 결과이고, 이번 작업에서는 중복 빌드/별도 DLL 링크나
C++/BP/백엔드 변경 없이 기존 러너의 선택 검증만 수행했다.

- `-ConnectionRecoveryOnly`: 복귀·비상 복구·입장 전 실패·정리 소유권·계획 종료 5개 통과.
  현재 컨트롤러의 스택 한정 정리 연결, 지워진 엔진 포인터, 다른 멤버/세대/포기/월드 종료
  제외, 실제 엔진의 입장 대기 CleanUp/Logout, 계획 신호의 즉시 Close/CleanUp,
  core ticker 대기/기한/교체 연결 보호를 확인했다.
- `-EjectionOnly`: 기존 Gameplay·Guards·Lifecycle·Reentry·MembershipMonitor 5개 통과.
  기존 영수증 기반 회수와 남은 파티의 신원/Pawn/연결 보존을 재확인했다.
- 두 러너의 종료 코드 0, 같은 DLL 해시, 닫힌 로그 해시와 각 러너의 소스 불변 검사를
  확인했다. 대기 전 선택 입력 13개가 그대로였고, 실행 증거의 합집합 69개와 DLL 사본을
  보관했다. 관련 C++ 5개 객체는 공유 빌드 로그의 실제 컴파일 및 DLL보다 이른 생성 시각을
  확인했다. 이전 `compiled_runtime_pending_shared_build` proof는 과거 기록으로 유지한다.

증거: `Saved/Tests/DungeonConnectionVerification-ead23f0565504ff6a2a1b14f84d15da6/`의
`Verification.proof.json`(`native_verified_shared_build`, `passed: true`), `SharedBuild.log`,
`ConnectionRecovery`/`EjectionRegression`의 닫힌 로그와 러너 proof,
`VerifiedInputs`/`VerifiedRuntime`이다.

테스트 종료 후 다른 작업에서 멤버십 스냅샷/모니터 관련 코드 4개가 변경되기 시작했다.
이번 proof는 보관한 소스/DLL의 실행 결과이며 그 이후 worktree의 추가 변경을 포함하지
않는다. 변경 파일은 proof의 `postTestWorkingTreeChanges`에 별도 기록하고, 통과한
실행 사본을 새 소스로 덮어쓰거나 이미 완료한 테스트를 무조건 반복하지 않았다.

이는 **소켓/패킷 없는 네이티브 fixture**다. 실제 RPC 전달, 비정상 클라이언트 종료→서버
단절 POST→PostgreSQL 시각 기록, 자연 10분 경과, UE의 만료 퇴장 명령 주기 호출/새
불변 기록 해석/실제 연결 회수, 서버 전체 장애·영속 보고·Steam·Shipping은 검증하지
않았다. RPC가 서버에 도달하지 않은 비상 복귀를 계획 종료로 보장한다는 주장도 하지 않는다.

## 만료 퇴장 캐시의 권한 회수·재입장 차단 (2026-10-17)

GameMode가 검증된 `disconnectionExpulsions` 캐시를 기존 권한 회수/연결 종료 경계에
연결했다. 정확한 세션·캐릭터·Steam ID의 불변 기록만 사용하고, 명시적 포기와는 별도
`dungeon_member_disconnected_expired` 사유를 유지한다. 멤버 부재·HTTP 오류·UE 로컬
시각으로 만료를 추측하지 않으며, 다른 멤버나 다른 세션/계정에는 적용하지 않는다.

- 멤버십 조회 완료와 기존 캐시 재처리는 두 퇴장 사유를 함께 처리한다. 신원/게임플레이
  권한을 먼저 회수한 뒤 현재 소유의 열린 소켓만 닫고, Pawn/Controller 제거와 Logout은
  엔진 정리에 맡긴다. child 연결이나 동기 콜백 중 교체된 소켓은 닫지 않는다.
- 티켓 소비 콜백·InitNewPlayer·입장 확인·RestartPlayer가 같은 캐시 사유를 검사한다.
  늦은 200/true 확인도 신원/PostLogin/Pawn을 다시 활성화하지 못한다. 신원/인벤토리
  관찰자의 동기 콜백 뒤에도 캐시를 다시 검사하며, 이미 퇴장된 신원을 새 단절 유예로
  보고하거나 계획 종료로 잘못 분류하지 않는다.
- 기존 명시적 포기 판정은 그대로 유지한다. 소급 보상 회수·거래 롤백·새 BP 액션이나
  저장 BP 자산 변경은 추가하지 않았다. 기존 GameMode 기반 BP의 네이티브 경로를 따른다.

공유 작업의 본문 없는 만료 POST→검증된 GET 순서는 위 절의 구현을 보존했다. 이번 절의
구현 범위는 캐시의 GameMode 적용과 그 테스트이며 전송 코드를 별도로 다시 구현하지 않았다.
공유 Editor 타깃에서 변경 C++ 2개의 컴파일/게임 DLL 링크가 포함된 65개 작업이 성공했고,
자체 `Project_RPGEditor Win64 Development` 재확인은 **0 action/up-to-date**로 성공했다.

같은 새 DLL에서 `-MembershipEjectionOnly` 8개(신규 만료 회수/가드 2개 포함), 기존
`-EjectionOnly` 5개, `-ConnectionRecoveryOnly` 5개가 모두 종료 0이다. 실제 엔진의
Close/CleanUp/Logout, 늦은 확인의 신원/Pawn 차단, 다른 계정/세션·잘못된 기록·조회 실패/
빈 응답·child 연결·교체 연결과 남은 파티 보존을 확인했다. 선택 입력 69개는 빌드/실행
동안 불변이고 세 러너의 DLL/닫힌 로그 해시가 일치한다.

증거: `Saved/Tests/DungeonExpiryEjection-ece350008fde44ffa481e80fce1f77a0/`의
`Verification.proof.json`(`expiry_cache_ejection_native_verified`, `passed: true`),
`Build.log`/`PrecedingSharedBuild.log`, `MembershipExpiry`/`EjectionRegression`/
`ConnectionRecovery`의 닫힌 로그·러너 proof와 `VerifiedInputs`/`VerifiedRuntime`이다.

검증은 소켓 없는 네이티브 fixture와 주입한 입장 확인 결과다. 실제 UE↔HTTP/PostgreSQL
POST→GET→Iris 연결 회수/재입장 거절·남은 2~4인 보상, 자연 10분 경과·서버 전체 장애/
재시작 내구성·Steam·Shipping의 완료 증거는 아니다. 다음은 이 네이티브 연결을 실제
개발 인증·티켓·백엔드/DB·클라이언트 단절 흐름에서 검증하는 단계다.

## 만료 재접속의 티켓 발급 차단과 로비 안내 (2026-10-18)

티켓을 발급한 뒤 서버 접속에서 거절하던 경로를 발급 단계까지 연결했다.
`POST /api/join-tickets`는 별도 사전 조회가 아니라 `StoreJoinTicketAsync`의 결과로
응답한다. Memory의 dungeon gate / PostgreSQL의 부모 세션 행 잠금 안에서 서버 시각을
다시 읽고, 정확한 계정·캐릭터·세션·활성 상태·lease·최초 단절 시각을 검사한 뒤에만
티켓을 기록한다. 포기/만료 퇴장/정산과 같은 잠금을 사용한다. 응답은 `no-store`다.

- 10분 미만의 재접속 발급은 허용한다. 티켓 발급만으로 단절 표식을 지우거나 입장
  세대를 올리지 않는다. 소비·입장 확인의 기존 만료 재검사도 유지한다.
- 정각부터 `409 {"error":"dungeon_reconnect_grace_expired"}`로 거절하며 티켓을
  저장하지 않는다. 정확한 본인의 불변 만료 퇴장 기록이 있으면 멤버 삭제/Closed 뒤에도
  같은 사유를 돌려준다. 다른 계정·미가입·명시적 포기·비활성 세션은
  `dungeon_member_not_active`로 구분하며 타 계정의 만료 기록을 드러내지 않는다.
- 거절은 멤버십/lease/기존 유효 티켓·보상 스냅샷을 변경하지 않는다. 단절 기록이 없는
  나머지 멤버는 계속 티켓을 발급받아 입장할 수 있다.

UE는 현재 인증/요청 세대의 정상 수신된 409에서만 위 두 코드를 허용한다. 실패 응답의
티켓·만료 값은 비우고, 잘못된 JSON·알 수 없는 코드·다른 HTTP 상태·전송 실패를 만료로
해석하지 않는다. 세션/캐릭터가 다른 완료는 현재 입장 요청을 해제하지 않는다.

`UHttpWebManager::OnJoinTicketIssued`의 기존 BP 이벤트 서명은 유지했다. 내부의 상세
완료를 던전 서브시스템에 연결하며, 기존 `OnFlowStateChanged` / `GetLastErrorCode`로
BP 결과를 받는다. 네이티브 로비에는 10분 유예 만료·현재 멤버 아님을 각각 표시하고
세션 새로고침을 안내한다. 이 실패만으로 캐릭터 선택/멤버십을 지우거나 포기·보상 회수를
실행하지 않는다. 저장된 BP 자산은 변경하지 않았다.

집중 실행: `dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests -c Release -- --join-ticket-issuance-only`.
새 Memory/실제 loopback HTTP 시나리오 5개와 gate 대기 중 정각 경계 1개가 통과했고,
기존 보상/입장/만료 회귀 22개도 통과했다. 백엔드/테스트 Release 빌드는 경고·오류 0이다.
PostgreSQL 구현 및 전용 행 잠금 검사는 컴파일됐지만 Docker가 꺼져 있어 실행하지 않았다.
새 UE 집중 검사 2개는 소스에 추가했으며 새 DLL 링크/실행 전에는 통과로 계산하지 않는다.
변경한 UE 구현 3개와 테스트 2개 C++의 컴파일은 통과했다. 전체 Editor 타깃은 이번에
수정하지 않은 `RPGAIController.cpp`의 멤버 가림 오류와 `RPGDungeonFlowE2EProbe.h/.cpp`의
반영 중인 클래스/생성 코드 오류로 실패했고 새 DLL 링크·네이티브 실행으로 계산하지 않는다.
빌드 로그/선택 입력: `Saved/Tests/JoinTicketIssuance-mvcmq0im/`.
실제 로비 입력·Steam·Shipping·자연 10분 전체 실행은 별도다.

## 단절 보고의 영속 큐 (2026-10-18)

`UHttpWebManager`의 서버 단절 보고를 파일 기반 outbox에 연결했다. 기존 GameMode가
정확한 소유 소켓의 예기치 않은 정리를 관측했을 때만 보고를 생성한다. 서버 전체
종료로 개별 단절을 추정하지 않으며, 재시작은 이미 저장한 보고만 복원한다.

- `Saved/Online/DungeonDisconnections/<backend hash>/<server hash>/<session>/`에
  캐릭터·Steam ID·정확한 int64 접속 세대·원래 HTTP 본문을 기록한다. 서비스 토큰은
  저장하지 않고 재기동 환경에서 읽는다. 동일 배정과 영속 Saved/UserDir가 필요하다.
- HTTP 전에 임시 파일 쓰기·디스크 flush·최종 이름 전환을 완료한다. 동일 세대의
  중복 보고는 기존 파일을 유지한다. 복원 시 전체 디렉터리의 범위·본문·파일명을
  검증한 뒤 완전한 임시 파일을 승격하며, 손상/다른 배정 파일은 보존하고 전송을 막는다.
- Core ticker가 유효한 게임 월드에서 보고를 전송한다. 초기 1초·3초 재시도 이후에도
  미확정 보고를 30초 간격으로 유지한다. 401·429·전송 실패·5xx·잘못된 200 응답은
  보고를 버리지 않는다. 전송 중 중복 보고도 새 요청을 만들지 않는다.
- 유효한 UTC `disconnectedAt`을 가진 200, 현재 멤버/세대 거절 403, 또는 정확한
  API 오류 코드를 가진 404/409에서만 해당 보고를 정리한다. 파일 정리가 실패하면
  같은 보고를 유지한다. 종료 시 HTTP 콜백/요청만 취소하고 디스크 보고는 남긴다.

이 큐는 단일 배정 서버의 재시작 복구다. 다중 프로세스의 같은 Saved 공유/호스트
이관 fencing은 제공하지 않는다. 최초 관측 시각 전달은 아래 2026-10-19 계약으로
추가했다. 실제 UE→HTTP/DB 재시작·지급/퇴장 통합은 아직 별도다.

집중 실행에서 기존 입장 코덱의 세 가지 오류도 확인해 수정했다. int64 초과 입력을
최대값으로 절삭해 수락하지 않도록 원래 숫자 문자열과 역변환 결과를 비교하고,
입장 확인은 JSON Boolean만 받는다. canonical GUID와 큐의 배정·본문 비교는
명시적인 case-sensitive 비교를 사용한다. 기존 코덱 회귀를 제거하거나 기대값을
완화하지 않았다.

검증: `Saved/Logs/DungeonDisconnectionJournalBuild-CodecFix-20261018.log`의 Editor
타깃 40개 작업과 최종 게임 DLL 링크가 Succeeded/종료 0이다.
`Saved/Tests/DungeonFlow/Automation-eabb8b16deb348889ec7b8da196d1ef8/DungeonFlow.proof.json`은
기존 복구/정리 5개·입장 코덱·새 영속 큐, 총 7개 Success/러너 종료 0을 기록했다.
새 객체에서 파일 복원, 정확한 큰 int64/본문 보존, 계정 교체·다른 서버/백엔드 거절,
완전한 임시 파일 복구, 손상 파일 보존, 해당 세대만 정리를 검사했다. proof의
소스·닫힌 로그·DLL 해시를 확인했고 DLL SHA-256은 `2B0263D3…59DAA9D`다.
이는 네이티브 파일 복구/분류 검사이며 실제 UE 프로세스 재기동·HTTP/DB 통합은 아니다.

초기 UHT 불일치로 중단한 빌드, 테스트 시작 전 180초 초기 로드 timeout, 기존
코덱이 실패한 `Automation-a41fe892fb8c4d3da608ce1b2f7ecb1f` 로그는 보존했다.
최종 로그에도 테스트 시작 전 `LogAutomationTest: Error: Condition failed` 15개가
남아 있으므로 프로젝트 기동 전체의 무오류 판정으로 확대하지 않는다. 기능 우선
요청에 따라 이 별도 기동 문제를 이유로 전체 테스트/E2E를 반복하지 않았다.

## 입장 거절 뒤 활성 멤버십 재조회와 로비 해제 (2026-10-18)

만료/비활성 멤버로 티켓 발급이 거절돼도 그 응답만으로 로컬 세션을 지우지 않는다.
대신 정확한 세션·선택 캐릭터의 거절 사유를 메모리의 UI 가드로 보관한다. 기존 오류
메시지가 다른 요청 실패로 바뀌어도 같은 인스턴스의 재입장은 잠기며, BP는 새 Pure 함수
`GetDungeonAdmissionBlockReason`으로 같은 가드를 읽을 수 있다. 서버 권한이나 포기
영수증을 만드는 함수가 아니며 백엔드 티켓 소비/입장 확인 경계는 유지한다.

로비의 기존 **새로고침 / 진행 중인 세션 복원** 버튼과 BP의 `RefreshDungeonSession` /
`ResumeDungeonSession`을 연결했다. 거절 가드가 있으면 접근 권한을 잃었을 수 있는 이전
세션 GET 대신, 선택 캐릭터의 인증된 `active-dungeon-session` GET을 사용한다.

- HTTP 실패·401·잘못된 멤버 응답에서는 이전 세션·선택·거절 가드를 유지한다.
- 같은 만료 세션에 멤버십이 아직 남아 있어도 재입장을 열지 않는다. 서버의 만료 퇴장
  처리가 끝날 때까지 다시 조회할 수 있다. 비활성 멤버 거절은 새 소유자 조회에서 같은
  활성 멤버십이 확인되면 새 티켓 요청을 허용한다.
- 인증된 소유자 조회의 204가 활성 세션 없음을 확인하면 이전 로컬 세션/가드만 해제하고
  `Idle`로 전환한다. 계정·선택 캐릭터를 보존하고 새 던전 생성/참가·캐릭터 선택을 연다.
  자기 캐릭터가 포함된 다른 활성 세션이 확인되면 그 세션을 복원하며 이전 인스턴스의
  거절 가드는 넘기지 않는다.

네이티브 로비는 가드가 있을 때 입장만 잠그고 조회/복원을 제공한다. 서버에서 활성
멤버십 없음이 확인되기 전에는 새 던전/캐릭터 전환을 열지 않는다. Steam 파티를 자동으로
떠나거나 멤버십을 삭제하는 POST, 보상 재지급·회수는 하지 않는다. 저장 BP 자산·백엔드
API/DB는 이번 작업에서 변경하지 않았다.

Editor 타깃은 UHT 생성과 72개 작업/새 게임 DLL 링크까지 종료 0으로 성공했다. 새 DLL의
입장 거절 Flow·Transport·MembershipRecovery와 기존 LobbyPolicy, 집중 4개 모두
Success다. 조회 실패/잘못된 멤버의 보존, 같은 만료 인스턴스의 재입장 차단, 소유자 조회의
활성 세션 없음/다른 활성 세션/비활성 거절 복구와 버튼 정책을 native fixture로 확인했다.
선택 소스 6개는 빌드 전 스냅샷부터 실행 후까지 불변이다. 실제 HTTP/DB·로비 클릭·Steam
통합 성공으로 계산하지 않는다.

단, 검사 시작 전 프레임 0에서 `LogAutomationTest: Error: Condition failed` 15개가
기록돼 전체 로그를 엄격히 검사한 wrapper는 종료 1이다. 선택 검사 구간의 오류는 0개이고
UE 검사 프로세스 자체는 종료 0이지만, 오류 없는 에디터 기동으로 표시하지 않는다.
기동 오류의 원인은 확정하지 않았고 무시 목록 추가·엔진 수정·검사 재실행은 하지 않았다.
증거: `Saved/Tests/DungeonAdmissionRecovery-mvcnfqj1/Verification.proof.json`,
`Build.log`, `automation.log`, `InputsBeforeBuild.json`.

## 단절 보고 최초 관측 시각 (2026-10-19)

배정된 UE 서버만 보내는 단절 POST에 선택 필드 `observedAtUtc`를 추가했다.
서버가 실제 소켓 정리를 관측한 UTC가 10분 계산의 기준이며, 클라이언트가 주장하는
크래시 시각은 받지 않는다. 네트워크 타임아웃으로 관측 자체가 늦어지는 시간까지
소급하는 기능은 아니다. UE와 백엔드 호스트의 UTC 시계 동기화를 운영 전제로 둔다.

- 활성 연결은 정확한 `Logout` 정리 시점에 캡처한다. 입장 확인 HTTP가 대기 중이면
  요청 상태에 시각을 한 번만 캡처하고, 늦은 콜백이 새 시각을 만들지 않게 전달한다.
- 새 outbox는 schema v2로 UTC 관측 시각과 본문을 함께 저장한다. 재시작 복원 때
  둘의 일치 여부를 검증하며, 재전송·중복 정리는 저장한 원래 본문을 유지한다.
  ISO 8601 밀리초 정밀도다. 기존 v1 파일은 시각을 만들어 넣지 않고 원래 본문으로
  재전송하며, 필드 없는 구형 요청은 기존 백엔드 수신 시각 계약을 유지한다.
- 백엔드는 세션 잠금 안에서 현재 소유자·접속 세대를 검사한 뒤 최초 시각만 저장한다.
  관측 시각으로 인증·세션 만료용 현재 시각을 대체하지 않는다. 최초 기록 이후 중복
  보고는 기한을 바꾸지 않으며, 과거 세대 거절과 입장 확인 tombstone은 유지한다.
- 새 최초 시각은 UTC여야 한다. 수신보다 5초 넘게 미래이거나 멤버십 시작보다
  5초 넘게 과거인 값은 400 `invalid_disconnection_observation`이다. 작은 경계 오차는
  `[멤버십 시작, 백엔드 수신]`으로 제한한다. 백엔드 시각이 멤버십 시작 전이면 거절한다.
  잘못된 시각의 outbox를 수신 시각으로 덮어쓰거나 확정 성공처럼 삭제하지 않는다.
- 10분 이후 처음 도착한 보고도 원래 시각을 저장한다. 이후 정산·입장·만료 처리는
  기존 기한 규칙을 적용하므로 해당 멤버만 보상 제외·재입장 차단·자동 탈퇴 대상이다.

범위 제한: 아직 도착하지 않은 보고를 백엔드가 알 수는 없다. 미전송 큐와 티켓 소비/
정산의 발행 순서 가드는 아래 후속 절에서 추가했다. 실제 UE→HTTP/DB 재기동 통합은
아직 별도다. 이미 확정된 보상 스냅샷을 뒤늦은 보고로 소급 취소하지 않는다.
서버 전체 장애를 개별 단절로 추정하지 않는 규칙도 유지한다.

검증: `dotnet run --project Backend/ProjectRpg.Backend.DungeonAbandonmentTests --
--disconnection-observation-only` 종료 0. InMemory 집중 8개와 실제 HTTP에서 9분 지연
관측·잘못된 시각 거절·서버 전용 권한·불변 재전송 검사를 통과했다.
로그: `Saved/Logs/DungeonObservationBackend-20261019.log`.
로컬 PostgreSQL 테스트 연결 미설정으로 DB 실행은 생략했다. PostgreSQL 구현도 같은 백엔드 빌드에
포함됐지만, 컴파일을 실제 DB 검증으로 계산하지 않는다.

UE 검증: `Saved/Logs/DungeonObservationBuild-20261019.log`에서 Editor 63개 작업과
게임 DLL 전체 링크 성공. 기존 러너의 `-ConnectionRecoveryOnly -SkipSDKSetup
-TimeoutSeconds 600`에서 집중 native 7개/러너 종료 0이다. 새 관측 시각 캡처,
v2 본문/시각 일치·불변 중복·파일 재복원, v1 무변경 호환을 기존 테스트에 추가했다.
proof: `Saved/Tests/DungeonFlow/Automation-bd48911dce7b4e00984f9074ae0672ee/DungeonFlow.proof.json`.
proof와 검사 후 DLL 해시는 일치한다. 이 실행은 실제 UE HTTP/DB 재기동 검증이 아니다.
검사 전 기존 `LogAutomationTest: Error: Condition failed` 15개는 계속 기록됐으며,
선택 테스트 성공을 에디터 전체 기동 무오류로 표시하지 않는다.

## 미전송 단절 보고와 입장·정산의 발행 순서 (2026-10-19)

배정별 `UHttpWebManager`가 건강한 outbox의 디스크/메모리 대기 보고가 모두 없을 때만
새 티켓 소비와 보상 정산 POST를 발행한다. 복원 전·손상된 큐·종료 중·영속 저장 실패도
통과시키지 않는다. 특정 멤버의 보상 박탈이나 파티 전체 탈퇴로 대신 처리하지 않는다.

- `PreLoginAsync`는 미전송 보고가 있으면 `dungeon_connection_reports_pending`으로
  거절하며 티켓 소비 API를 호출하지 않는다. 기존 로컬 연결 복구·멤버십 재조회 흐름으로
  돌아가 재시도할 수 있다. 전송 계층 자체에도 같은 가드가 있어 다른 호출자도 우회하지
  못한다. 전송 실패를 명시적 포기나 보상 제외 영수증으로 만들지 않는다.
- GameMode는 정산 발행 전에 가드를 확인한다. 대기 중에는 보상 버전·재화/아이템
  목록·정산 요청 상태를 유지하고, in-flight로 만들지 않는다. 기존 heartbeat 재시도가
  큐 해소 뒤 다시 발행한다. 직접 전송 호출의 보류는 로컬 상태
  `WaitingForDisconnectionReports`/425/false이며, 실제 백엔드 응답이 아니다.
  425는 기존 재시도 가능 분류를 사용하고 던전 실패·보상 요청 취소를 만들지 않는다.
- HTTP 수락과 해당 파일 정리가 모두 성공한 뒤에만 보고가 해소된다. 401·429·5xx·
  전송 실패·잘못된 200과 오래된 요청의 콜백은 가드를 풀지 못한다. 두 명의 단절이
  대기 중이면 한 명의 보고만 해소돼도 아직 발행하지 않는다. 기존 확정 거절 규칙은 유지한다.
- 단절 보고·입장 확인·heartbeat·읽기 전용 멤버십 조회는 이 가드로 막지 않는다.
  따라서 보고 전송/복구가 자기 자신을 기다리는 교착이나 남은 파티의 일괄 권한 회수가 아니다.
- 입장 확인 HTTP 대기 중의 소켓 정리는 응답을 기다리지 않고 이미 소비한 정확한 세대와
  관측 UTC를 즉시 영속 큐에 넣는다. 성공하면 늦은 확인 콜백은 다시 보고를 만들지 않는다.
  확인 요청과 단절 POST의 도착 순서는 기존 backend generation/tombstone이 처리한다.
  영속 저장 실패 시 빈 큐로 오인하지 않도록 현재 서버 프로세스의 mutation 가드를 닫는다.
  이 실패 상태는 단순 큐 재조회/다른 보고 성공으로 자동 해제하지 않는다.

경계: 이는 **이미 관측/큐잉한 보고와 이후 발행할 요청** 사이의 가드다. 관측 전에
이미 발행된 HTTP 요청을 취소하거나 커밋을 되돌리는 분산 트랜잭션 장벽이 아니다.
그 경합, 실제 UE→HTTP/DB 전송·응답 유실·재기동 통합은 별도다. 영속 기록을 끝내
쓰지 못한 상태의 프로세스/호스트 손실은 복구 성공으로 주장하지 않는다. 서버 전체
종료로 아직 관측하지 않은 개별 단절을 추정해서 생성하지 않는 정책도 유지한다.

검증: 공유 최종 빌드 `Saved/Tests/DungeonDifficulty-mve2mmpo/FinalBuild.log`에서 새
검사항목과 GameMode 재컴파일을 포함한 6개 작업/게임 DLL 링크·Editor 타깃 성공을
확인했다. 최초 검사항목의 누락 include는 보완했다. 자체
`Saved/Logs/DungeonDisconnectionOrderingBuild-20261019.log`는 최신 상태 0개 작업/종료 0이다.
기존 러너의 `-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600`에서 새
`DungeonDisconnectionOrdering`을 포함한 native 8개/러너 종료 0이다. 디스크 전용/복원된
보고의 차단, 실패 응답·오래된 콜백의 보존, 읽기 복구 허용, 전원 해소 뒤 동일 정산
본문의 재개, 쓰기 실패 뒤 빈 큐 오인 방지를 production 요청 생성/콜백으로 검사했다.
실제 `ProcessRequest`/소켓/HTTP/DB 전송은 이 native 검사에서 실행하지 않았다.
proof: `Saved/Tests/DungeonFlow/Automation-faedc0c6f9d54a0d9d19e84e3cbf9a94/DungeonFlow.proof.json`.
검사 후 DLL 해시 일치·선택 소스 변경 0개를 확인했다. 검사 전 기존
`LogAutomationTest: Error: Condition failed` 15개는 남으며 기동 무오류로 표시하지 않는다.
백엔드 변경이 없어 이전 백엔드 검사는 반복하지 않았다.

## 진행 중인 입장·정산 요청의 완료 경합 (2026-10-19)

단절 관측 전에 이미 발행된 티켓 소비/정산은 백엔드의 세션 잠금·현재 세대·최초 단절
시각으로 판정한다. UE가 그 결과를 취소/되돌리는 분산 트랜잭션은 추가하지 않는다.
대신 현재 요청과 배정에 맞는 완료를 정확히 한 번 적용하도록 전송 계층에 연결했다.

- 같은 티켓의 소비는 한 요청만, 정산은 한 요청만 진행한다. 중복 호출이 원래 요청에
  실패 이벤트를 보내거나, 뒤늦은 단절 큐 때문에 진행 중인 정산에 로컬 425를 보내
  in-flight 상태를 해제하지 않는다. 다른 티켓의 입장까지 일괄 취소하지 않는다.
- 요청을 만들 때 백엔드 URL·서버·세션·월드를 캡처한다. 완료 시 같은 요청 객체가
  아직 현재 요청인지 먼저 확인하고, 현재 배정/월드와 일치할 때만 결과를 전달한다.
  다른 세션을 담은 티켓 소비 응답은 성공으로 전달하지 않는다. 서비스 토큰 갱신
  자체는 이미 확정된 요청의 소유 범위를 바꾸지 않는다.
- 현재 요청의 슬롯은 완료 전달 전에 해제한다. 따라서 실패 처리에서 재시도를 시작해도
  이전 요청의 중복/지연 콜백이 새 슬롯을 지우거나 새 요청에 실패를 덮어쓰지 않는다.
  HTTP 발행 자체가 실패한 경우도 슬롯을 해제해 무기한 진행 중으로 남지 않는다.
  엔진의 `ProcessRequest(false)`가 같은 스레드에서 완료 콜백을 먼저 호출하는 경로도
  있으므로, 콜백과 시작 실패 처리가 같은 완료 소유권 검사를 사용한다. 이미 콜백이
  처리했거나 새 요청이 슬롯을 차지했다면 이전 시작 실패를 다시 전달하지 않는다.
- 이미 발행된 요청의 정상 성공은 나중에 단절 보고가 생겨도 적용한다. 정산 성공을
  보류/실패로 덮거나 지급을 취소하지 않으며, 나중 단절 기록도 임의로 삭제하지 않는다.
  이후의 새 요청만 기존 outbox 가드가 기다린다. 응답 유실 뒤 정산 재시도는 기존
  동일 버전/본문의 백엔드 멱등 계약과 단절 큐 해소 조건을 유지한다.
- 서브시스템 종료는 진행 중 소비/정산의 콜백을 해제하고 요청을 취소한다. 이미 큐에
  올라온 콜백도 현재 요청 확인을 통과할 수 없다. 이는 로컬 수명 정리이며 백엔드
  커밋 롤백이나 개별 플레이어 단절 추정이 아니다. 영속 단절 파일은 그대로 남긴다.

이 단계 당시에는 실제 엔진 PreLogin 완료 전 원래 소켓 유실의 소비 세대 보존도
별도였다. 해당 추적은 아래 후속 항목에서 연결했다. 소비 응답 자체의 유실/프로세스
손실 복구와 실제 UE→HTTP/DB·Iris 통합은 여전히 별도다.
이번 연결은 새 백엔드 판정 시각/보상 정책을 도입하거나 기존 확정 스냅샷을 변경하지 않는다.

검증: 최종 `Saved/Logs/DungeonInFlightMutationBuild-CompletionFix-20261019.log`에서
전송 코드 증분 컴파일·게임 DLL 링크·Editor 타깃 종료 0. 초기 전체 컴파일에서 별도
난이도 검사의 `TObjectPtr` 전달이 빌드를 막아 `.Get()` 한 줄만 보완했으며 검사 의미를
완화하지 않았다. 시작 실패 중복 완료 경로 보완 뒤 최종 바이너리로 기존
`-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` 러너를 실행해
`DungeonInFlightMutationCompletion`을 포함한 native 9개/종료 0을 확인했다.
중복 호출/완료, 나중 단절 후 성공 보존, 잘못된 세션/배정 응답 거절, 새 요청을 유지하는
오래된 콜백, 종료 후 이미 큐에 오른 콜백의 격리를 production 요청·관찰자로 검사했다.
proof: `Saved/Tests/DungeonFlow/Automation-82b925b9a871433d98d5a6feb95aa4fb/DungeonFlow.proof.json`.
검사 후 DLL 해시 일치·선택 소스 변경 0개다. 실제 HTTP/DB/소켓 경합은 실행하지 않았다.
기존 기동 전 `Condition failed` 15개는 남으며 기동 무오류로 표시하지 않는다.

## PreLogin 입장 전 원래 연결의 유실 추적 (2026-10-19)

티켓 소비 요청부터 `InitNewPlayer`/`PostLogin`의 입장 확인 요청 인계까지 원래
부모 `UNetConnection`과 driver·PlayerId·대소문자를 구분한 티켓을 함께 추적한다.
엔진의 PreLogin 완료 delegate에서 연결을 꺼내거나 IP 주소만으로 다른 연결을 고르지
않는다. 실제 `NMT_Login`이 저장한 RequestURL/PlayerId와 현재 authoritative driver를
검사해 유일한 원래 연결을 찾지 못하면 소비 API를 호출하기 전에 거절한다.

- 아직 살아 있는 `LoggingIn`/`Welcomed` 연결의 로딩 시간은 단절로 계산하지 않는다.
  모호한 중복 티켓·child/shared 연결·소유자가 이미 붙은 연결은 새 입장으로 선택하지 않는다.
- 매 서버 틱에 원래 연결의 Close/CleanUp/소멸을 관측해 최초 UTC를 한 번만 기록한다.
  티켓 소비 응답을 기다리는 동안 관측했어도 같은 추적 상태를 보존한다. 단순 목록 이탈,
  URL/신원 변경, driver 교체나 월드 종료만으로는 개인 단절을 생성하지 않는다.
- 소비 성공에서 **정확한 접속 세대를 알게 된 경우** 해당 세대와 최초 관측 UTC를 기존
  영속 단절 큐로 넘긴다. 저장/전송 계층이 준비되지 않으면 승인 정보와 시각을 유지하고
  30초 간격으로 다시 시도한다. 실제 저장 성공 전에는 큐에 들어간 것으로 표시하지 않는다.
- 죽거나 바뀐 연결에는 엔진 PreLogin 완료를 호출하지 않는다. 같은 티켓으로 새 연결이
  생겨도 이전 승인을 넘겨주거나 관측 시각을 초기화하지 않는다. 관측한 유실은 불가역이다.
- `InitNewPlayer`는 검증된 승인을 즉시 삭제하지 않는다. 정확한 원래 연결이 `PostLogin`에서
  입장 확인 요청을 시작한 뒤 기존 confirmation/Logout 추적으로 인계한다. 생성 실패나
  그 전 연결 유실로 `Logout`이 없더라도 소비한 승인 정보가 추적에서 빠지지 않게 한다.
  입장 확인 전 신원/스폰을 허용하지 않는 기존 정책은 유지한다.
- 종료 시 이전 엔진 완료 delegate는 해제한다. 서버 전체 종료를 개인 단절로 대체하거나
  남은 파티를 회수하지 않으며 이미 확정된 보상을 변경하지 않는다.

남은 경계: 소비 응답 자체를 잃어 커밋/세대를 모르는 경우와 영속 쓰기 전에 프로세스가
사라진 경우는 이 기능의 복구 성공에 포함하지 않는다. 실제 UE→HTTP/DB·Iris 접속/퇴장
통합도 native 연결 생명주기 검사와 별도다.

검증: 공유 빌드 `Saved/Tests/BackendDifficultyFlow-mvf8n4h8/Build.log`에서 이번
GameMode/연결 검사 재컴파일과 게임 DLL 링크를 포함한 Editor 95개 작업/종료 0을
확인했다. 자체 `Saved/Logs/DungeonPreLoginRecoveryBuild-20261019.log`는 그 결과의
최신 상태 확인으로 0개 작업/종료 0이다. 기존 러너의
`-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600`에서 신규
`DungeonPreLoginConnectionRecovery`를 포함한 native 10개/종료 0을 확인했다.
정확한 원래 연결 선택·중복/공유 연결 거절, 살아 있는 로딩의 보존, 응답 전 최초 단절
시각, 실제 native Close/CleanUp 뒤 대체 연결 격리, 전송 불가 시 정확한 int64 세대와
관측 UTC 유지/재시도, 월드/driver 교체 비추정 및 남은 파티 보존을 검사했다.
소비 성공 데이터만 fixture로 주입했으며 실제 소비 HTTP·영속 보고 HTTP·DB·소켓을
실행한 검사가 아니다. proof:
`Saved/Tests/DungeonFlow/Automation-a3606d940f3a4567829a1389feea8772/DungeonFlow.proof.json`.
검사 후 DLL 해시 일치·선택 소스 변경 0개다. 기존 기동 전 `Condition failed` 15개는
남으며 기동 무오류로 표시하지 않는다. 백엔드 변경이 없어 관련 suite는 반복하지 않았다.

## 소비 응답 유실의 동일 요청 복구 API (2026-10-19)

이 단계는 **백엔드 기반 구현**이었다. 당시 UE의 ID 발행/자동 재시도는 미연결이었으며,
아래 후속 항목에서 연결했다. 실제 UE/HTTP/DB 통합 검증과는 구분한다.

- 기존 `POST /api/join-tickets/consume`에 선택적 UUID `consumptionRequestId`를 추가했다.
  배정 게임 서버가 한 입장 요청마다 한 번 만들고, 그 요청의 재시도 동안 동일하게 써야 한다.
  생략한 이전 호출은 계속 일회용이다. 빈 UUID는 400이며 티켓을 소비하지 않는다.
- 첫 소비에서 티켓 삭제·접속 세대 증가·결과 영수증 저장을 같은 세션 잠금/트랜잭션으로
  처리한다. 동일 서버·세션·요청 ID·티켓 해시의 재시도만 이전 결과를 그대로 돌려준다.
  다른 ID의 재소비와 같은 ID로 다른 티켓 소비는 거절하며 다른 티켓을 소진하지 않는다.
  응답 경로를 production `MapJoinTicketApi`로 모아 실제 HTTP 검사에서도 같은 코드를 쓴다.
- 복구 허용 기간은 **최초 소비 후 10분**이며 원래 티켓의 짧은 TTL과 독립적이다.
  다만 서버 인증, 배정·세션 상태, 현재 멤버·캐릭터 lease, 현재 접속 세대와 단절 유예는
  다시 검사한다. 이 기간은 멤버십/서버 임대나 단절 유예를 연장하지 않는다.
- 더 새 세대가 생겼거나 명시적 포기·10분 단절 만료·정산 등으로 입장 자격이 사라졌다면
  이전 승인을 복구하지 않는다. 복구는 입장 확인이 아니며 단절 시각·확인 세대·파티나
  보상 상태를 쓰지 않는다. 기존 거절 응답과 동일하게 유효한 결과가 없으면 401이다.
- PostgreSQL은 `game_join_ticket_consumptions`에 해시·요청 UUID·배정/멤버·세대·시각만
  저장한다. 원본 티켓/서비스 토큰을 저장하지 않는다. 초기에는 후속 소비가 만료 영수증을
  정리했지만, 아래 기한 후 증거 조회 단계에서 재시도 기한과 증거 보관을 분리했다.
  종료 세션까지 포함한 전역 보관 주기/운영 migration은 별도다.
  메모리 저장소는 같은 gate 안에서 구현하며 프로세스 재시작 영속성을 주장하지 않는다.
- 새 UE 호출을 켜기 전에 스키마와 모든 백엔드 인스턴스를 먼저 갱신해야 한다. 이전
  백엔드가 새 요청 ID를 무시하고 소비하면 복구 영수증이 생기지 않는다.

검증: Release 빌드 및 `ProjectRpg.Backend.DungeonAbandonmentTests`의
`--join-ticket-consumption-only`에서 메모리 9개 그룹/종료 0. 실행 중인 loopback 개발 DB의
생성한 별도 `rpg_abandon_test_*` 스키마에서 `--postgres-only --require-postgres`로
PostgreSQL 동일 9개 그룹과 영수증 INSERT 실패의 전체 롤백·새 저장소/연결 복구 1개 그룹,
총 10개/종료 0을 확인했다. 각 저장소 그룹에 production endpoint의 실제 HTTP 인증·
반복 POST 검사가 포함된다. 16개 동시 요청의 단일 세대, 티켓 TTL 이후 복구·정확한
복구/단절 만료·이전 세대/포기/정산 차단·남은 파티도 검사했다. HTTP 첫 응답은 호출자
논리 경계에서 버리고 반복 요청한 것으로, 실제 TCP 패킷 유실이나 UE/Iris 검사는 아니다.
로그: `Saved/Logs/JoinTicketConsumptionRecovery-Postgres-20261019.log`.
검증용 임시 스키마만 정리했으며 기존 개발 DB 데이터는 보존했다. 최초 테스트 코드의
private helper 접근 컴파일 오류는 해당 검사 내부의 예외 확인으로 보완했다.
UE 변경이 없어 Editor 빌드/기존 native·보상 전체 suite는 반복하지 않았다.

이 단계의 후속 기능이었던 UE의 UUID/동일 요청 재시도와 기존 단절 보고 흐름의 인계는
아래에서 연결했다. 프로세스가 요청 ID와 원래 연결 관측을 잃는 경우의 재시작 복구는
여전히 별도다.

## UE 동일 입장 요청의 응답 복구 재시도 (2026-10-19)

배정 게임 서버의 `UHttpWebManager::ConsumeJoinTicket`가 최초 요청에 UUID를 한 번
발행하고 백엔드 URL·서버·세션·월드·HTTP 본문과 함께 메모리에서 보존한다. 이후
재시도는 새 HTTP 객체를 사용하되 같은 UUID와 본문을 그대로 보낸다. 서비스 토큰은
현재 값을 사용하므로 정상적인 자격 갱신이 원래 요청의 배정을 바꾸지는 않는다.

- 전송 실패·408/425/429·5xx·해석 불가능하거나 세션이 다른 2xx는 불확실 응답이다.
  원래 GameMode에 아직 실패 완료를 보내지 않고 1초→3초→이후 30초 간격으로 재시도한다.
  기존 1초 core ticker가 발행하며, 최초 요청 시점의 단조 증가 600초 deadline은 갱신하지 않는다.
- 처음 정상 수신한 401은 소비하지 못한 요청이므로 바로 실패 처리한다. 이전 불확실
  응답 이후의 일반 401은 자격 검증/미확정 상태일 수 있어 재시도한다. 단, 인증된
  consume endpoint의 정확한 `401 {"error":"join_ticket_not_recoverable"}`는 현재 세대의
  결과를 복구할 수 없다는 확정 거절로 처리한다. 이전 세대/포기된 요청이 남은 파티를
  10분 동안 묶지 않도록 구분한 것이다. 이 표식은 새 백엔드에서 추가했으며 HTTP 상태와
  이전 호출의 일회용 정책은 유지했다. 복구 deadline 이후에는 결과 부재를 커밋 부재로
  해석하지 않도록 아래의 미확정 안전 가드를 유지한다.
- 불확실한 소비 결과가 하나라도 남으면 새 티켓 소비와 새 정산 발행을 보류한다.
  이미 발행한 소비/정산 성공을 덮지 않고 기존 정산 버전·본문·재시도 상태는 유지한다.
  읽기 전용 멤버십·heartbeat·단절 보고·이미 시작된 입장 확인은 계속 허용한다.
  소비 재시도 자체도 건강하고 비어 있는 단절 outbox를 기다리므로 발행 순서를 우회하지 않는다.
- 중복 호출은 원래 요청에 실패를 보내거나 UUID/deadline을 바꾸지 않는다. 각 완료가
  현재 HTTP 객체의 소유권을 한 번만 가져가며, 이전 시도의 지연/중복 콜백은 새 시도를
  지우거나 끝낼 수 없다. `ProcessRequest(false)`의 동기 콜백/시작 실패도 같은 검사를 쓴다.
- 복구 성공은 기존 GameMode 티켓 완료로 전달한다. 중간 실패 완료가 없으므로
  PreLogin의 원래 연결·최초 단절 관측 UTC는 유지된다. 복구된 정확한 int64 세대는 기존
  승인/단절 경로로 인계하며, 죽은 원래 연결 대신 새 연결에 승인을 넘기지 않는다.
  불확실성 발행 가드는 동기 완료 알림 동안에도 닫아, GameMode가 이미 관측한 유실을
  영속 큐에 넣기 전에 새 정산이 끼어들지 못하게 한다.
- 배정/월드 변경이나 종료에는 이전 결과를 새 GameMode에 통보하지 않는다. 종료는
  대기 intent를 제거하고 현재 요청/콜백을 취소하며, 기존 영속 단절 파일은 보존한다.
  UUID·원본 티켓 본문은 메모리 전용이며 원본 티켓/서비스 토큰을 로그로 추가하지 않는다.
- 불확실한 결과를 끝내 600초 안에 확인하지 못하면 해당 입장은 실패 완료하되 새로운
  세대/단절 보고를 추정하지 않는다. 큐가 비어 있어도 새 입장/정산을 허용하지 않는
  프로세스 안전 가드를 유지한다. 다른 요청 성공이나 단순 큐 해소로 자동 해제하지 않는다.
  다른 파티원의 액터/전투 권한이나 확정 보상을 회수하는 처리는 아니다. 이 상태의 운영
  조정과 intent/연결 관측 자체를 잃은 프로세스 재시작 복구는 아직 별도 작업이다.

배포 순서: 모든 백엔드 인스턴스와 소비 영수증 스키마를 먼저 갱신하고 새 UE 서버를
배포해야 한다. 구형 백엔드가 UUID를 무시한 채 소비한 결과는 이 경로로 보장하지 않는다.

검증: `Saved/Logs/DungeonConsumeResponseRecoveryBuild-20261019.log`에서 UHT 생성
4개 파일과 전송/새 검사·생성 코드 재컴파일을 포함한 Editor 24개 작업·게임 DLL/전체
타깃 링크 성공·종료 0이다. 앞선 공유
`Saved/Tests/NormalDifficulty-5d918e74/Build.log`의 설정 생성 코드 관련 컴파일 실패는
보존했으며 자체 재생성 후 재현되지 않았다. 해당 설정 소스는 이번 작업에서 수정하지 않았다.
최종 DLL의 `-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600`에서 신규
`DungeonConsumeResponseRecovery`를 포함한 native 11개/종료 0이다. 같은 UUID/본문/
deadline 유지, 이전 콜백 격리, 미확정 소비의 발행 장벽·읽기 복구 허용, 단절 outbox 순서,
잘못된/유실 응답의 보존, 정확한 int64 세대와 동기 인계 장벽, 최초/미확정/확정 표식 401의
구분, 배정 변경·기한 만료 가드·종료 후 콜백 취소를 실제 요청 생성/주입 콜백으로 검사했다.
이 native 검사에서는 `ProcessRequest`/실제 HTTP·DB·소켓 전송을 실행하지 않았다.
proof: `Saved/Tests/DungeonFlow/Automation-80e98db78bc74b27b49d858a3aad2a8d/DungeonFlow.proof.json`.
검사 후 DLL 해시 일치·선택 소스 변경 0개다. 기존 기동 전 `Condition failed` 15개는 남는다.

백엔드의 확정 거절 표식은 Release 빌드와 기존 소비 복구 집중 메모리 9개 그룹(같은
production endpoint의 실제 HTTP 포함)/종료 0에서 확인했다. 로그:
`Saved/Logs/DungeonConsumeRecovery-DenialHttp-20261019.log`. 저장소/SQL 변경이 없어
PostgreSQL·보상 전체 suite는 반복하지 않았다. 따라서 UE가 실제 HTTP 응답 유실 뒤
재시도하고 원래 연결 유실을 보고하는 전체 네트워크 통합이나 Steam/Shipping의 증거는 아니다.

## 복구 기한 이후의 소비 증거 조회 (2026-10-19)

이 절은 **백엔드 기반 구현**의 계약이다. 후속 UE 연결 범위는 아래
`UE 기한 초과 intent의 증거 조회·단절 큐 인계` 절을 따른다. 요청/연결 관측을 잃은
프로세스 재시작이나 증거 없는 건의 운영 복구까지 완료됐다는 뜻은 아니다.

- 일반 `consume` 재시도 허용은 계속 최초 소비 후 10분이다. 대신 그 기한이 지났다는
  이유로 소비 영수증을 삭제하지 않는다. 과거 세대·원래 요청 ID/해시·소비 시각을 유지하고
  ID는 같은 배정/세션에서 재사용하지 않는다. 기존 세션/캐릭터/계정 FK 삭제 정책은 유지한다.
  이미 이전 배포가 정리한 기록을 복원하거나 메모리 저장소의 재시작 영속성을 추가한 것은 아니다.
- 배정 서버 전용 `POST /api/join-tickets/consumption-evidence`를 추가했다. 입력은
  `dungeonSessionId`, `serverId`, `consumptionRequestId`, `ticketHash`, `expectedSteamId`다.
  SHA-256 hex는 64자리 ASCII hex만 허용하고 백엔드의 대문자 형식으로 정규화한다.
  원본 티켓/서비스 토큰을 새로 저장하거나 조회 응답으로 내보내지 않는다.
- 인증된 현재 서버·세션·요청 ID·해시와 **원래 연결에서 관측한 Steam ID**가 모두 맞아야
  기록을 돌려준다. 훔친 다른 계정의 티켓만으로 그 캐릭터의 단절을 보고하는 우회를 막기
  위한 계약이며, 후속 UE 구현도 Steam ID를 응답에서 거꾸로 가져와 채우면 안 된다.
  살아 있는 배정의 Loading/InProgress에서만 조회하고 종료 후 telemetry-only 권한은 확장하지 않는다.
- 응답은 `{ state: "Recorded", consumption: { dungeonSessionId, serverId,
  consumptionRequestId, characterId, steamId, connectionGeneration, consumedAt } }`다.
  일반 입장 응답과 다른 envelope로 분리했다. 이는 **과거 소비 증거이지 현재 입장 허가가 아니다**.
  만료·더 새 세대·포기·단절 뒤에도 과거 기록은 조회할 수 있으나 신원 활성화나 확인 세대/
  단절 시각·멤버십·보상·lease를 변경하지 않는다. 소비 시각은 단절 관측 시각이 아니다.
- PostgreSQL은 admission/정산과 같은 부모 세션 행 잠금을 얻은 뒤 현재 시각과 배정을
  다시 검사한다. 읽은 뒤 상태가 바뀔 수 있으므로, 후속 단절 보고도 기존 API에서 정확한
  현재 세대를 재검증해야 한다. 조회가 포기나 확정 보상을 되돌리는 근거가 될 수 없다.
- 기록 없음·이전 정리·legacy 일회용 소비·요청/해시/Steam 불일치는 모두 409
  `join_ticket_consumption_unresolved`다. 조회가 요청을 봉인하거나 취소하지 않으므로
  409를 “소비되지 않았다”는 불변 증거로 해석하거나 UE 발행 가드를 풀면 안 된다.
  응답은 `no-store`이며 잘못된 입력은 400, 플레이어/다른 배정은 403이다.
- 저장 스키마 구조는 바꾸지 않았지만 모든 백엔드 노드를 먼저 갱신해야 한다. 구형 노드의
  정리 코드가 남아 있으면 증거가 다시 지워질 수 있다. 기존에 지워진 기록, 증거 보관 운영
  주기, 만료/재발급이 필요한 서버 자격과 요청 관측 자체를 잃은 재시작 복구는 별도다.

검증: Release 빌드 후 `--consumption-evidence-only` 메모리 8개 그룹, 실제 loopback
PostgreSQL의 별도 스키마에서 같은 8개+새 저장소/연결 영속성·행 잠금 후 현재 시각 재검증
1개 그룹/모두 종료 0. 각 저장소에 production endpoint의 실제 HTTP 인증·배정/입력/원래
Steam ID/해시·기록 부재·캐시 금지·입장 응답과의 envelope 분리 검사가 포함된다.
10분 뒤 다른 소비에도 증거 보존, ID 재사용 거절, 과거 세대/포기/단절 조회의 무변경,
남은 파티 보상과 기존 유예 만료도 확인했다. 보관 정책 변경의 직접 회귀로 기존 소비 집중
메모리 9개·PostgreSQL 9개+원자적 롤백/새 연결 복구 1개 그룹도 종료 0이다.
로그: `Saved/Logs/JoinTicketConsumptionEvidence-Memory-20261019.log`,
`Saved/Logs/JoinTicketConsumptionEvidence-Postgres-20261019.log`,
`Saved/Logs/JoinTicketConsumptionEvidence-ReplayRegression-20261019.log`.
검증용으로 생성한 두 `rpg_abandon_test_*` 스키마만 정리했으며 기존 개발 DB 데이터는 보존했다.
최초 검사 코드의 nullable application name 컴파일 오류는 필수 값 검사로 보완했다.
UE 코드 변경이 없어 Editor/native/실제 Iris 검사는 반복하지 않았다.

후속 UE도 기한을 넘긴 UUID/배정·원래 Steam 연결과 실제 단절 관측 시각을 보존해야 한다.
확인한 세대의 영속 단절 큐 인계 전에는 가드를 풀지 않는 계약은 그대로 유지한다.

## UE 기한 초과 intent의 증거 조회·단절 큐 인계 (2026-10-19)

- 일반 소비 재시도 기한이 지나도 원래 UUID/배정/월드·티켓 해시·소비 전 연결에서 얻은
  Steam ID를 메모리에 유지한다. 엔진 PreLogin 실패는 한 번만 전달하고 완료 delegate는
  해제하지만 원래 연결 추적은 보존한다. 기한 초과 자체는 단절이 아니며, 그 뒤의 실제
  Close/CleanUp도 관측할 수 있다. 다른 연결로 재매칭하거나 서버 전체 종료를 개인 단절로
  만들지 않는다. 프로세스가 사라지면 이 메모리 관측도 사라지는 경계는 남는다.
- 원래 소켓의 최초 실제 단절 UTC가 있어야 증거 조회를 발행한다. 원래 Steam ID가 없는
  개발 우회 경로는 응답으로 신원을 채우지 않으며 계속 안전 보류한다. 조회 본문에는
  원문 티켓 대신 UTF-8 SHA-256 대문자 hex를 넣고, OpenSSL을 재사용한다. 티켓·서비스
  자격·미확정 intent를 새로 디스크에 기록하거나 로그에 출력하지 않는다.
- 30초 간격으로 조회하되 정확한 요청 HTTP 객체/intent·현재 배정/월드를 검사한다.
  409 기록 부재와 인증/통신 실패·손상/다른 Steam/서버/세션/UUID 응답은 미확정으로 유지한다.
  일반 입장 응답과 구분한 codec은 숫자 int64 세대를 그대로 읽고 `consumedAt`의 UTC
  `Z`/`+00:00` 형식을 허용한다. 소비 시각을 단절 시각으로 사용하지 않는다.
- 증거는 절대로 입장 성공 이벤트·캐릭터 인증/스폰·입장 확인으로 전달하지 않는다.
  검증된 과거 캐릭터/세대와 원래 Steam/단절 UTC만 기존 영속 outbox에 넘긴다. 백엔드의
  단절 API가 여전히 현재 세대/멤버십/불변 보상을 재검사하므로 과거 세대 조회가 현재
  접속이나 포기·확정 보상을 되돌리지 않는다.
- 조회 성공만으로 intent를 제거하지 않는다. 큐 저장 실패 시 확인된 세대와 원래 UTC를
  유지해 저장을 재시도하고, 영속 소유권 인계 후 해당 intent 하나만 제거한다. 다른 미확정
  intent·남은 outbox·기존 영속 쓰기 실패 가드는 계속 새 입장/정산을 막는다. 인계 중 동기
  ticker/콜백 재진입도 가드를 유지한다. 읽기·heartbeat·단절 보고는 계속 허용한다.

검증: 최종 Editor 전체 타깃/게임 DLL 링크 종료 0, 집중 native 12개/러너 종료 0.
proof는 `Saved/Tests/DungeonFlow/Automation-f70ba51287c9456eb411024dc29eda5e/DungeonFlow.proof.json`이다.
실행 뒤 DLL SHA-256 일치, 선택 소스 21개 변경 0을 확인했다. 최초 단절 UTC/원래 peer,
큰 int64·UTC 응답 형식, 다른 배정/UUID/Steam/손상 응답, 기록 부재·인증/통신 실패,
저장 불가 시 intent 보존과 재시도, 다른 미확정 intent/남은 outbox의 보류, 정확한 확정
거절과 빈 409의 구분, 만료 후 원래 연결 관측·엔진 실패 1회/종료 후 늦은 콜백을 검사했다.
이 검사는 실제 production 요청/콜백과 임시 디스크 outbox를 사용하지만 HTTP 응답은 주입하며
실제 소켓/UE↔HTTP/DB/Iris 통합 실행은 아니다. 기존 기동 `Condition failed`/Error 15줄은
그대로 남아 있어 엄격한 기동 오류 0 통과로 해석하지 않는다. 백엔드 코드는 바꾸지 않았고
이전 백엔드 HTTP/PostgreSQL 검사를 반복하지 않았다.

빌드/수정 이력: 첫 모듈 재컴파일에서 새 fixture의 중첩 delegate 타입명을 보완했다.
같은 빌드 도중 옵션 UI 헤더가 변경돼 생성 헤더 줄 번호가 어긋난 것은 다음 UHT의 5개
생성 파일 갱신으로 맞췄다. 그 뒤 남은 `RPGOptionMenu.cpp`의 동기 위젯 순회 람다에는
`this` 캡처 한 곳만 추가해 컴파일을 복구했다. 기존 옵션 동작/다른 변경은 보존하며 옵션 UI
런타임 회귀를 검증했다는 뜻은 아니다. 게임 DLL 링크 후 첫 native 실행의 새 테스트는 빈
409를 확정 거절로 가정해 실패했다. 운영 보류 로직은 유지하고 실제
`dungeon_disconnection_not_available` 표식과 빈 409의 보존 검사를 사용하도록 fixture를 고쳤다.
수정 후 테스트 파일 컴파일 포함 4개 작업/전체 링크와 위 12개 검사를 통과했다.
빌드 로그는 `Saved/Logs/DungeonConsumeEvidenceRecoveryBuild-20261019.log`,
`DungeonConsumeEvidenceRecoveryBuild-Final-20261019.log`,
`DungeonConsumeEvidenceRecoveryBuild-Link-20261019.log`,
최종 `DungeonConsumeEvidenceRecoveryBuild-TestFixture-20261019.log`에 있다.
첫 native 실패 로그는 `Saved/Tests/DungeonFlow/Automation-357b192b7d5640d48ee55997b6866f9a/automation.log`다.

다음: 원래 요청/연결 관측 자체를 잃는 재시작, 증거가 없는 미확정 건과 영속 쓰기 실패의
운영 복구, 실제 UE↔HTTP/DB·Iris의 응답 유실/기한 경계·남은 파티 정산 연결이다.

## 이미 관측한 미확정 단절의 재기동 복원 (2026-10-20)

범위는 **원래 소켓의 실제 단절을 관측했고 저장까지 완료한 소비 미확정 건**이다.
서버 전체 종료/프로세스 장애를 개인 단절로 추정하는 기능이 아니며, 단절을 관측하기 전의
요청이나 디스크에 기록하기 전에 사라진 관측은 복원하지 못한다.

- `FRPGDungeonConsumeJournal`이 같은 배정의 outbox 하위 `ConsumeRecoveries/`에
  요청 UUID·UTF-8 티켓 SHA-256·원래 Steam ID·최초 단절 UTC와 backend/server/session
  scope만 저장한다. 원문 티켓·소비 본문·서비스 토큰·원래 연결 객체/엔진 delegate는 저장하지
  않는다. 같은 UUID의 해시/peer/UTC 변경은 거절하며 최초 관측을 갱신하지 않는다.
- 실제 단절 전달 시 flush→임시 파일 이름 전환으로 저장한다. 복원은 모든 파일과 동일
  main/tmp 복사본을 먼저 검증하며 손상/충돌/다른 scope는 부분 적용·자동 삭제 없이 보류한다.
  예상하지 못한 파일/디렉터리, 과도한 개수·크기, 저장소 접근 실패도 안전 보류한다.
- 재기동 시 원문 티켓 없는 별도 intent로 복원해 일반 consume/입장 성공을 재실행하지
  않는다. `Game` 타입 초기화용 더미 월드는 건너뛰고 BeginPlay가 완료된 정확한 배정의 첫
  게임 월드에 한 번만 연결한다. 이후 월드 변경/
  종료·오래된 HTTP 완료는 기존 scope 규칙으로 격리한다. 기존 소비 증거 API만 조회하며
  기록 부재나 인증 실패는 미커밋 증거로 해석하지 않는다.
- 일반 소비 응답이 늦게 도착해도 이미 단절을 저장한 peer는 입장 성공으로 전달하지
  않는다. 정확히 일치하는 성공 세대만 보고 큐에 넘기며, 확정되지 않은 결과는 같은 증거
  복구 경로에 남긴다. 다른 Steam ID의 응답으로 원래 신원을 채우지 않는다.
- 소비 단절 기록의 제거 순서는 **단절 outbox 영속 저장 → 원래 소비 단절 파일 정리**다.
  중간 장애로 두 파일이 함께 복원돼도 원래 UTC/세대/본문으로 중복 인계를 처리한다.
  저장·정리 실패나 어느 한 큐의 미해소 건이 남으면 새 입장/정산을 계속 보류한다.
  인계 중 즉시 HTTP 전송 대신 기존 1초 ticker가 전송을 맡아 중간 상태의 재진입을 막는다.
- 같은 배정/저장소는 한 활성 게임 서버 프로세스가 소유해야 한다. 미해소 기록이 남아
  있으면 새 ledger를 모르는 구버전 서버로 롤백/혼용하지 않는다. 다중 프로세스 fencing과
  기록 없는 건의 자동 봉인은 이번 변경이 제공하지 않는다.

검증: 최종 Editor 전체 타깃/게임 DLL 링크 종료 0, 집중 native 14개/러너 종료 0.
최종 proof는 `Saved/Tests/DungeonFlow/Automation-2846b465f3a2486dbc56a248560061b1/DungeonFlow.proof.json`이다.
실행 뒤 DLL SHA-256 일치, 선택 소스 24개 변경 0을 확인했다. 신규 두 검사는 실제 임시 파일의
저장/새 저장소 복원·중복/서로 다른 main/tmp·손상·다른 scope·저장 경로 차단 시 원본 보존,
새 manager/WorldContext의 원래 UUID/hash/Steam/UTC 복원, 더미 월드/종료 중 보류, 일반
consume 재전송·역사적 입장 성공 금지, 증거 부재 시 파일 불변, outbox와 두 파일이 함께
남은 장애 경계 및 늦은 일반 소비 성공의 단절 인계를 확인했다. 단절을 관측하지 않은 live
요청은 종료 때 저장/단절 보고로 만들지 않는 것도 확인했다.

이는 파일/새 UObject/실제 WorldContext로 재현한 native 복원이며 HTTP 응답/기존 outbox의
장애 경계는 fixture로 주입한다. 실제 OS 프로세스 강제 종료·서버 재할당/자격 재발급·
HTTP/DB/Iris 통합의 증거가 아니다. 기존 확정 보상·남은 파티 권한·멤버십 판정 API는
바꾸지 않았고 백엔드 suite/통합 검사를 반복하지 않았다. 기존 기동 `Condition failed`/
Error 15줄도 남아 있어 엄격한 기동 오류 0 통과로 표시하지 않는다.

빌드 이력: 첫 자체 빌드는 fixture의 `final GetWorld` 재정의와 동시에 변경된
PlayerController 헤더/생성 매크로의 줄 번호 불일치로 실패했다. fixture는 실제 protected
WorldContext 연결 방식으로 보완했고 PlayerController 소스는 수정하지 않았다. 공유 생성
헤더 갱신 후 매크로 59행 일치를 확인한 자체 47개 작업/전체 링크가 성공했다. 초기화 더미
월드의 BeginPlay 대기 보완 뒤 5개 작업/전체 링크·첫 14개 native가 통과했다. 저장 경로의
접근 실패/파일에 막힌 상위를 빈 저장소로 오인하지 않도록 마지막 5개 작업/전체 링크와
위 14개를 다시 확인했다. 첫 통과 proof는
`Saved/Tests/DungeonFlow/Automation-4197773fbdeb4d10ad345ead82b91d1b/DungeonFlow.proof.json`이다.
빌드 로그는 `Saved/Logs/DungeonConsumeObservedLossRestartBuild-20261020.log`,
`DungeonConsumeObservedLossRestartBuild-Final-20261020.log`,
`DungeonConsumeObservedLossRestartBuild-ReadyWorld-20261020.log`,
최종 `DungeonConsumeObservedLossRestartBuild-Storage-20261020.log`에 남긴다.

남은 기능: 관측 전 프로세스 장애의 별도 처리, 영속 쓰기 실패 복구, 단일 배정/저장소의
다중 프로세스 fencing과 실제 재기동 통합이다. 미소비 intent의 제한적 봉인 백엔드 기반은
아래에서 추가했으며 UE 인계와 원본 티켓/기록이 모두 없는 건의 운영 복구는 남는다.

## 남아 있는 미소비 티켓의 원자적 취소·봉인 (2026-10-20)

이 절은 **백엔드 기반 단계**의 계약이다. 후속 UE 연결은 아래 절을 따르며 기존 읽기
전용 `consumption-evidence`의 409를 미소비 확정으로 바꾸지는 않는다.

- 배정 서버 전용 `POST /api/join-tickets/seal-consumption`을 추가했다. 입력은 기존 증거
  조회와 같은 세션·서버·요청 UUID·SHA-256 해시·원래 연결의 Steam ID다. 현재 배정의
  Loading/InProgress에서만 허용하며 종료 후 telemetry-only 권한을 넓히지 않는다.
- consume/정산과 같은 메모리 gate 또는 PostgreSQL 부모 세션 행 잠금 안에서 처리하고,
  잠금 대기 후 현재 시각·배정을 검사한다. 해당 UUID의 소비 영수증이 이미 있으면 정확한
  해시/Steam 일치 때만 `{ state: "Recorded", consumption: {...}, cancellation: null }`을
  반환한다. 과거 세대/유예 만료/포기 후 기록도 역사적 증거이며 입장 허가가 아니다.
- 소비 영수증이 없더라도 **원본 해시의 미소비 티켓이 실제로 남아 있고 세션/Steam이
  일치할 때만** 티켓 삭제와 취소 영수증 저장을 원자 처리한다. 남아 있는 만료 티켓도
  이 증거가 될 수 있으나, 먼저 TTL 정리를 하거나 정리된 부재에서 같은 결론을 내리지 않는다.
  다른 UUID의 소비/취소 기록이 같은 해시를 점유한 경우도 취소로 만들지 않는다.
- 성공은 `{ state: "Cancelled", consumption: null, cancellation: { dungeonSessionId,
  serverId, consumptionRequestId, ticketHash, expectedSteamId, cancelledAt } }`이다.
  원본 티켓/토큰을 저장하지 않는다. 취소 영수증은 해시와 배정별 UUID를 예약하며,
  늦게 도착한 같은 UUID, 다른 UUID, UUID 없는 legacy consume 모두 취소된 해시를 소비하지
  못한다. 같은 UUID로 다른 티켓을 태우는 것도 막는다. 정확한 재요청은 최초 취소 시각과
  증거를 유지한다. 별도 `game_join_ticket_consumption_cancellations` 테이블과 기존 FK를 쓴다.
- 원본 티켓과 영수증이 모두 없거나 legacy/이전 배포의 영수증 정리·신원/해시 충돌이면
  409 `join_ticket_consumption_unresolved`를 유지하며 새 취소 영수증도 만들지 않는다.
  미래 요청만 봉인했다고 과거 커밋까지 없었다고 주장하는 방식은 사용하지 않는다.
  잘못된 입력 400, 다른 신원/배정 403, endpoint 응답 `no-store`다.
- 취소는 해당 입장 intent에만 적용한다. 세대·확인/단절 시각·멤버십·lease·보상은 변경하지
  않는다. UE 연결 시에도 정확한 취소 증거를 확인하고 해당 관측 파일의 영속 정리를 마친
  뒤 그 intent만 제거해야 한다. 다른 미확정 intent/outbox/저장 실패 가드를 함께 풀면 안 된다.
  `Recorded`는 기존 정확한 세대/원래 단절 UTC의 outbox 인계 경로로 처리해야 한다.

검증: Release 실행에서 신규 집중 `--consumption-seal-only` 메모리 8개 그룹 통과.
같은 production endpoint의 실제 loopback HTTP에서 인증/배정/입력/중첩 응답·캐시 금지,
취소 응답 유실 재요청과 뒤늦은 세 종류의 consume 거절을 검사했다. 불변 취소 시각,
다른 티켓/신원 보호, 만료 후 남은 티켓과 정리된 부재의 구분, 과거 세대·단절/포기·남은
파티 보상도 확인했다. 직접 영향 회귀인 기존 소비 메모리 9개와 증거 조회 8개 그룹도 통과했다.
로그: `Saved/Logs/JoinTicketConsumptionSeal-Memory-Final-20261020.log`,
`JoinTicketConsumptionSeal-ReplayRegression-20261020.log`,
`JoinTicketConsumptionSeal-EvidenceRegression-20261020.log`.

PostgreSQL 동일 8개 그룹과 INSERT 실패 롤백/새 저장소 복구·봉인/소비 양방향 잠금 순서·
잠금 후 시각 만료·이전 삭제 영수증의 미확정 유지 5개 그룹은 작성했지만 **미검증**이다.
loopback 54329 리스너/DB 프로세스가 없고 실행은 DB 연결 단계의 NpgsqlException으로
종료 1이었다(`Saved/Logs/JoinTicketConsumptionSeal-Postgres-20261020.log`). 임시 스키마
생성 전 실패했으며 기존 DB는 변경하지 않았다. UE 변경이 없어 Editor/native/HTTP↔UE·Iris
전체 검사는 반복하지 않았다.

배포 전 스키마와 모든 백엔드 노드를 먼저 갱신하고 PostgreSQL 집중 검사를 통과시켜야 한다.
구형 노드가 취소 UUID를 무시하는 혼합 배포는 이 보장의 범위가 아니다. UE의 취소 증거
검증·파일 정리는 아래에서 연결하며, 티켓/소비 증거 모두 없는 건의 운영 복구는 별도다.

## UE 취소 증거 검증·미확정 파일 정리 (2026-10-20)

- 실제 연결 단절을 관측한 기한 초과 intent의 증거 조회에서 transport 성공·정확한 409
  `{"error":"join_ticket_consumption_unresolved"}`를 받으면, 다음 30초 재시도부터 같은
  scope/UUID/hash/원래 Steam ID 본문으로 `seal-consumption`을 호출한다. 일반 409·인증 거절·
  실패 전송·조회 endpoint의 취소 형태 200만으로 봉인하거나 가드를 해제하지 않는다.
- 취소 응답은 200 `Cancelled`·null `consumption`·중첩 `cancellation`이 모두 있어야 한다.
  정규 UUID, 64자리 대문자 ASCII 해시, Steam/서버 형식, UTC 취소 시각을 검증하고 현재
  원래 intent의 세션/서버/UUID/hash/Steam과 정확히 비교한다. 소비 증거와 취소 증거가 함께
  있는 모순된 응답은 거절한다. 취소 시각을 원래 단절 시각으로 대체하지 않는다.
- 검증 후에도 해당 관측 파일의 삭제가 성공하기 전에는 intent를 제거하지 않는다.
  삭제 실패는 원본/보류를 유지하고 30초 뒤 **파일 정리만** 다시 시도한다. 세대/단절 보고를
  추정하거나 일반 consume/입장 성공 이벤트를 실행하지 않는다. 다른 intent, 단절 outbox,
  sticky 저장 실패 가드는 그대로 유지하며 정리 임계 구역 중 새 입장/정산도 막는다.
- 봉인보다 소비가 먼저 커밋된 `Recorded`는 기존 정확한 int64 세대·최초 관측 UTC의
  영속 outbox 인계 경로로 간다. HTTP 객체/intent를 한 번만 claim하고 이전 배정/지연 콜백/
  종료 응답은 파일을 정리하지 못한다. 부재 409·구형 API 404·401/403·5xx는 미확정이다.
- 취소 성공과 로컬 삭제 사이에 재기동하면 원래 관측 파일을 복원하고 다시 증거 조회→
  동일 UUID/hash 봉인으로 확인한다. 이전 메모리의 취소 확인을 복원하거나 원본 티켓을
  재소비하지 않는다. 재기동 자체를 새 개인 단절로 간주하지 않는 원칙은 유지한다.

검증: 자체 Editor 전체 타깃/게임 DLL 링크 종료 0
(`Saved/Logs/DungeonConsumeCancellationBuild-20261020.log`, 5개 증분 작업).
동시 공유 72개 작업 빌드에서 새 테스트가 잘못 사용한 `IFileManager::SetReadOnly` 컴파일
오류를 확인해 `IPlatformFile::SetReadOnly`로 보완했다. 해당 공유 빌드는 실패 이력을
보존하며, 이후 자체 전체 링크가 성공했다. HTTP/codec 객체의 앞선 공유 컴파일을 재사용했다.

집중 `-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` native 15개/러너 종료 0.
proof: `Saved/Tests/DungeonFlow/Automation-0db824d8f98348b0be91969116931b1f/DungeonFlow.proof.json`.
실행 뒤 DLL SHA-256 일치·선택 소스 24개 변경 0을 확인했다. 신규 취소 검사와 강화한 codec/
재기동 검사에서 정확한 409 전환·동일 본문/30초 간격·다중 intent, 잘못된 신원/해시/배정/
모순된 증거/구형 API 실패·지연 콜백 격리, 실제 읽기 전용 파일의 삭제 실패·파일만 재시도,
소비 승자의 int64/원래 UTC outbox 인계·ACK 전 보류, sticky 저장 실패 가드 보존과 종료 후
파일 보존을 확인했다. 취소 확인 뒤 로컬 삭제 실패 상태에서 새 manager/실제 WorldContext로
복원해 같은 UUID/hash 봉인 증거를 다시 받고 정리하는 장애 경계도 검사했다.

이 native 검사는 HTTP 완료를 주입하며 실제 `ProcessRequest`/HTTP·DB·소켓 전송은 하지 않는다.
기존 기동 `Condition failed`/Error 15줄은 남아 있어 엄격한 기동 무오류 통과가 아니다.
백엔드 코드는 변경하지 않아 기존 suite는 반복하지 않았다. 앞선 PostgreSQL 집중 검사는
여전히 미검증이며 배포 전 스키마/모든 노드 갱신과 DB 검증이 필요하다. 실제 OS 강제 종료·
UE↔HTTP/DB·Iris 2~4인 통합, 관측 전 장애·티켓/소비 기록 모두 없는 건의 운영 복구와
다중 프로세스 fencing은 별도다. 관측 기록 저장 실패의 재시도·보류 복구는 아래에서 연결한다.

## UE 미확정 단절 관측 기록의 저장 재시도 (2026-10-20)

범위는 소비 결과가 미확정인 원래 연결의 **실제 단절 관측 메타데이터**다. 이미 세대가
확정된 단절 outbox의 쓰기 실패 복구나, 관측/flush 전 프로세스 장애를 해결한 것은 아니다.

- 관측 파일 저장에 실패하면 원래 intent를 별도 메모리 큐가 보유한다. 기존 HTTP/입장
  콜백 맵이 정리돼도 UUID/hash/원래 Steam/최초 단절 UTC는 그대로 남는다. 새 시각/신원을
  만들거나 원본 티켓/서비스 토큰을 파일·로그에 추가하지 않는다.
- 기존 core ticker에서 1초→3초→이후 30초 간격, 한 번에 최대 4건을 재시도한다.
  해당 소비 재발행·증거 조회/인계는 관측 저장을 기다린다. 이미 발행한 소비 성공은 정확한
  세대의 역사적 증거로 보존하며 입장 성공으로 바꾸거나 파일 저장 전 outbox로 넘기지 않는다.
- 재시도는 원래 backend/server/session/world와 journal 배정이 모두 같을 때만 쓴다.
  배정/월드 교체·종료에는 다른 scope에 재저장하거나 큐를 성공한 것으로 지우지 않는다.
  시작 시 복구 기록이 없는 접근 실패 자체를 새 개인 단절로 만들지도 않는다.
- I/O 실패 후 journal은 디렉터리 전체와 기존에 영속화했다고 알고 있는 모든 기록을
  다시 검사한다. 완전하고 같은 `.tmp`는 검증 후 승격할 수 있지만 기존 기록의 부재/
  변경·부분 JSON·손상·충돌은 원본을 남기고 실패한다. 빈 디렉터리를 보고 과거 영속 기록이
  없었다고 판단하거나 불완전한 파일을 자동 삭제/덮어쓰지 않는다.
- 같은 관측의 저장 성공 때 그 쓰기 대기 항목만 해제한다. 디스크 큐가 비었다는 이유로
  원인 불명/충돌의 sticky 실패 가드를 초기화하지 않는다. 소비 미확정 기록·다른 intent·
  단절 outbox도 별도 보류 조건이며, 기존 증거/취소 검증과 정확한 파일 정리/ACK까지
  완료해야 새 입장/정산이 재개된다. 남은 파티 전투 권한·멤버십/보상 판정은 바꾸지 않는다.

검증: 최종 자체 Editor 전체 타깃/게임 DLL 링크 종료 0
(`Saved/Logs/DungeonConsumeObservationWriteRecoveryBuild-Final-20261020.log`, 4개 증분 작업).
첫 81개 작업 빌드는 동시에 수정된 `SoundManager.h`의 17행과 이전 생성 매크로 27행의
불일치로 종료 6이었다(`DungeonConsumeObservationWriteRecoveryBuild-20261020.log`). 해당
소스는 수정하지 않았고 공유 UHT 갱신의 17행 일치·관련 객체 재컴파일 뒤 최종 링크가
성공했다. 마지막 bounded batch 순회 조정이 포함된 HTTP 객체도 최종 빌드에서 재컴파일했다.

집중 `-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` native 16개/러너 종료 0.
proof: `Saved/Tests/DungeonFlow/Automation-f8d18bdcffdd43629d0ffe38b75bbe18/DungeonFlow.proof.json`.
실행 뒤 DLL SHA-256 일치·선택 소스 24개 변경 0을 확인했다. 새 저장 복구 검사와 강화한
journal 검사에서 실제 임시 경로를 디렉터리로 막아 파일 열기 실패를 유도하고, 두 미저장
관측과 기존 영속 기록 보존·최초 UTC/peer/hash 불변·1/3/30초 재시도·배정 변경/종료 보류,
손상된 임시 파일 보존·경로 복구 후 재저장, 이미 발행한 소비 성공의 정확한 세대 보존과
후속 outbox 인계·다른 intent/ACK 전 보류·모두 해소한 뒤 재개를 확인했다. 기존에 영속화한
파일이 사라지거나 올바른 형식의 다른 신원으로 바뀌어도 재시도 복구가 가드를 풀지 않고,
완전히 같은 임시 기록만 승격하는 것도 확인했다. 빈 재시도 큐는 원인 불명 sticky 실패를
초기화하지 않으며 종료 후 미저장 관측을 새 scope에 쓰지도 않는다.

HTTP 응답은 fixture로 주입했으며 실제 `ProcessRequest`/HTTP·DB·소켓을 보내지 않았다.
기존 기동 `Condition failed`/Error 15줄은 남는다. 백엔드 변경이 없어 해당 suite는 반복하지
않았고 앞선 PostgreSQL 봉인 검사도 여전히 미검증이다. 저장 전 프로세스 강제 종료로
사라진 메모리 관측, 원본 티켓/소비 증거 모두 없는 건, 다중 프로세스 fencing과 실제
UE↔HTTP/DB·Iris 통합은 별도다. 이미 확정된 세대의 단절 outbox 쓰기 실패 복구는 아래에서 연결한다.

## UE 확정 세대 단절 outbox의 저장 재시도 (2026-10-20)

- 실제 관측을 전달받은 `ReportConfiguredDungeonMemberDisconnection`는 캐릭터/Steam/
  정확한 int64 접속 세대·최초 UTC·전송 본문을 메모리에 보존한다. 파일 저장 실패는 계속
  false이며 호출자가 사라지더라도 별도 대기 큐가 원래 보고를 보유한다. 중복 호출의
  나중 시각/다른 Steam으로 덮지 않는다. 기존 v1 보고의 알 수 없는 시각도 새로 채우지 않는다.
- 서비스 토큰은 로컬 기록 저장의 조건과 분리했다. 실제 보고는 토큰이 잠시 없더라도
  영속화할 수 있지만 HTTP 요청은 현재 유효한 형식의 토큰을 요구한다. true는 전송 성공이
  아니라 **영속 큐에 들어갔음**을 뜻한다. 소켓/세대/신원 관측 자체의 호출 경계는 유지한다.
- 기존 ticker에서 1초→3초→이후 30초, 최대 4건의 순환 배치로 저장을 재시도한다.
  미저장 보고는 원래 backend/server/session/world와 journal 배정이 모두 같은 경우만 쓴다.
  다른 배정/월드나 종료에는 원본을 성공으로 지우거나 새 개인 단절로 재작성하지 않는다.
- 저장소 회복은 전체 디렉터리와 이미 영속화한 기록의 동일성을 검사한다. 접근 불가/
  사라진 기존 파일/변경된 신원·본문/손상된 임시 파일/보고 경로의 예상 밖 디렉터리는
  그대로 남기고 실패한다. 완전히 같은 임시 파일만 검증 후 승격한다. 독립 하위 journal인
  정확한 `ConsumeRecoveries` 디렉터리는 parent outbox 기록으로 읽지 않으며 소비 journal의
  별도 저장/미확정 가드는 계속 유지한다.
- 저장 성공은 해당 쓰기 대기 항목만 영속 HTTP 큐로 인계한다. 디스크의 정확한 보고와
  본문이 맞아야 HTTP 요청을 만들며, 예전 요청 객체/배정/월드의 완료는 현재 파일을 정리하지
  못한다. 이미 영속화한 동일 배정 보고의 재전송은 기존 재기동/월드 인계 정책을 따른다.
  정확한 200 ACK나 기존 명시적 terminal 응답과 파일 삭제가 끝나기 전에는 새 입장/정산을
  보류한다. 다른 보고/소비 intent·원인 불명 sticky 쓰기 실패도 자동 해제하지 않는다.

검증: 최초 Editor 83개 작업/전체 링크 종료 0 이후 첫 집중 native에서 새 검사 1개가
실패했다. 토큰을 요구하는 공통 `IsConfiguredForDungeonServer` 조건이 로컬 저장까지
막고 있어, 저장 경로만 서버/세션 설정 검사로 분리했다. 수정 HTTP 객체의 재컴파일은
성공했지만 다른 작업의 2인 검증 서버/클라이언트가 DLL을 사용 중이라 LNK1104/종료 6이었다.
해당 프로세스는 종료하지 않았고 정상 종료 뒤 자체 DLL 링크/타깃 metadata 2개 작업이
성공했다(`Saved/Logs/DungeonDisconnectionWriteRecoveryBuild-Ready-20261020.log`, 종료 0).
앞선 빌드/잠금 실패는 `DungeonDisconnectionWriteRecoveryBuild-20261020.log`,
`DungeonDisconnectionWriteRecoveryBuild-Final-20261020.log`에 보존한다.

수정본 집중 `-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` native 17개/
러너 종료 0. proof:
`Saved/Tests/DungeonFlow/Automation-529e12025d534de89fc6953581fbd436/DungeonFlow.proof.json`.
실행 뒤 DLL SHA-256 일치·선택 소스 24개 변경 0을 확인했다. 실제 파일 열기 실패와 부분
임시 파일을 통한 두 미저장 보고 보존/재저장·기존 파일 불변, 정확한 int64/최초 UTC 본문,
중복 시각/잘못된 Steam 거절·배정 변경/지연 콜백 격리·토큰 부재 시 로컬 저장/HTTP 차단,
ACK/명시적 terminal 전 보류와 모든 보고 해소 뒤 재개를 확인했다. 강화한 journal 검사도
기존 파일 유실/올바른 형식의 다른 신원으로 변경 시 보존, v1 미상 시각과 완전한 임시
파일 승격, 독립 소비 하위 journal·예상 밖 경로·접근 불가를 구분했다. 원인 불명 sticky
실패는 빈 대기 큐로 초기화하지 않으며, 종료 후 미저장 보고를 새 scope에 쓰지 않는다.
첫 native 실패 로그는
`Saved/Tests/DungeonFlow/Automation-ff277aa9fd61497f876043f645e7786b/automation.log`다.

이번 native는 HTTP 완료를 주입했으며 실제 `ProcessRequest`/HTTP·DB·소켓 전송 증거가
아니다. 기존 기동 `Condition failed`/Error 15줄은 남는다. 백엔드/멤버십/보상 판정은
변경하지 않아 관련 전체 suite는 반복하지 않았다. 앞선 PostgreSQL 봉인 집중 검사는
여전히 미검증이다. flush 전 프로세스 장애, 다중 프로세스 fencing과 실제 UE↔HTTP/DB·
Iris 통합은 별도다. 시작 시 journal 복원 실패의 재검사·자동 재개는 아래에서 연결한다.

## UE 시작 시 journal 복원 실패의 재검사와 자동 재개 (2026-10-20)

- 시작 시 단절 outbox 또는 하위 소비 관측 journal을 읽지 못해도 ticker를 설치한다.
  원래 backend/server/session의 같은 저장소를 1초→3초→이후 30초 간격으로 재검사한다.
  재검사 자체는 HTTP·현재 토큰·게임 월드를 요구하지 않으며, 배정이 달라지거나 종료되면
  복원을 진행하지 않는다. 종료 시 ticker와 보유 핸들도 해제한다.
- 시작 복원 대기와 이후의 개별 쓰기 실패를 분리했다. 어느 저장소든 복원 대기라면 새 입장/
  정산은 보류하며, 실제 관측한 새 단절은 메모리에 보존한다. 쓰기 재시도가 초기 복원을
  우회해 파일만 읽고 보고 맵을 비워 두는 경로를 막았다. 손상/부분 파일·경로 차단은 그대로
  실패하고 원본을 보존한다. 완전히 같은 임시 기록만 전체 검증 후 승격한다.
- 실패한 저장소만 복원한다. 이미 복원한 다른 저장소의 요청 객체·콜백·ACK/파일 삭제 상태는
  초기화하지 않고, 처리 완료한 보고를 다시 생성하지 않는다. 게시할 기록과 기존 메모리가
  충돌하면 부분 게시 대신 보류하며 원인 불명 실패 가드를 초기화하지 않는다.
- 소비 관측 복원은 원래 UUID/hash/Steam/최초 UTC를 유지한 만료 intent만 만든다. 초기화의
  더미 Game 월드를 건너뛰고 첫 BeginPlay 월드에 한 번 바인딩한 뒤 기존 증거 조회/취소 봉인/
  outbox 인계를 재개한다. 티켓 재소비·입장 성공이나 서버 전체 장애의 개인 단절을 만들지
  않는다. 복원 성공만으로 정산을 열지 않으며 남은 intent와 보고 ACK도 모두 해소해야 한다.

검증: 자체 Editor 타깃 70개 작업/전체 링크 성공 뒤 최종 HTTP manager 소스 재컴파일·
lib/DLL/metadata 4개 작업도 종료 0이었다. 최종 빌드 로그는
`Saved/Logs/DungeonJournalStartupRestoreBuild-Ready-20261020.log`다.
`-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` 집중 native 18개/러너 종료 0.
proof: `Saved/Tests/DungeonFlow/Automation-c461efd1d8fc4236b765046296157043/DungeonFlow.proof.json`.
신규 검사는 양쪽/parent만 초기 복원 실패·자동 ticker 설치·1/3/30초 간격·부분 파일 보존/
명시적 fixture 복구·배정 변경/토큰 부재·미저장 실제 관측의 후속 영속화·다른 저장소의 진행 중
요청 보존/ACK 재생성 금지·더미 월드 차단/증거 조회 재개·종료 후 재시도 차단을 확인했다.

러너 완료 시 DLL·선택 소스 24개 불변을 확인했고, 이후 DLL과 이번 수정 4개도 proof와
일치했다. 단 proof 작성 후 별도 작업의 `RPGPlayerController.cpp` 변경 1건이 확인됐다.
이 결과는 검증 시점 바이너리의 증거이며 그 후의 전체 소스 트리 빌드 성공을 뜻하지 않는다.
기존 기동 `Condition failed`/Error 15줄은 남는다. 새 manager/실제 격리 파일과 주입된
HTTP 완료의 native 검사이며 실제 OS 프로세스 재시작·HTTP/DB/Iris·Steam 통합은 아니다.
백엔드는 변경하지 않아 전체 suite를 반복하지 않았고 PostgreSQL 봉인 집중 검증도 남는다.
실제 HTTP/DB 복구 연결과 당시 미검증이던 PostgreSQL 봉인 검사는 아래에서 이어간다.
증거 없는 intent의 운영 복구·저장 전 장애·다중 프로세스 fencing·남은 파티의 실제
지급/퇴장 통합은 별도다.

## UE 프로세스 재시작의 실제 HTTP와 PostgreSQL 복구 연결 (2026-10-20)

운영 API를 바꾸지 않고 명시적 opt-in UE 자동화 진입점과
`Scripts/Network/Run-DungeonConsumeHttpRestartTest.ps1`을 추가했다. 개발 전용 서버 권한
조건의 서로 다른 UE 프로세스 두 개에서 기존 소비/증거 조회/outbox 전송을 실제로 호출한다.
검사 호스트는 loopback에만 바인딩하고 journal은 실행별 GUID 폴더에 격리한다. 토큰/원본
티켓은 자식 환경 변수로만 전달하며, 저장한 journal에 포함되지 않는지 검사한다.

- 첫 소비를 실제 API/DB가 커밋한 뒤 응답을 끊는다. 첫 UE는 원래 UUID/hash/Steam/관측
  UTC를 저장하고 정상 종료한다. 새 UE는 raw ticket 없이 기록을 복원해 역사적 증거만
  조회한다. 소비 POST가 총 1회인지, 입장 완료 콜백이 없는지 검사한다.
- 복원된 단절 보고의 첫 200 ACK도 DB 커밋 뒤 유실시킨다. 같은 신원/세대/최초 UTC/본문의
  재전송이 ACK를 받은 뒤에만 두 journal 파일이 모두 없어지고 입장/정산 가드가 열린다.
- 명시적 11분 시간 가속 뒤 실제 만료 API로 해당 캐릭터만 자동 탈퇴시키고 미정산 대상에서
  제외한다. 남은 파티원의 연결 상태를 유지하고 정산 대상도 그 파티원만 남는지 확인한다.
  이때 정산은 기존 enqueue 대상 검사이며 아이템/재화의 최종 지급 완료 증거는 아니다.

재현: 먼저 Editor 타깃을 빌드한 뒤
`Scripts/Network/Run-DungeonConsumeHttpRestartTest.ps1 -Postgres -NoRestore -TimeoutSeconds 240`.
`-Postgres`가 없으면 메모리 저장소이며 DB 검증으로 계산하지 않는다. PostgreSQL 모드는
기존 loopback 개발 DB의 소유 검증된 임시 스키마만 만들고 정리한다. Docker나 다른 작업의
에디터/프로세스를 시작·종료하지 않으며 자체 자식만 수명/시간 제한을 관리한다.

검증: 자체 Editor 최초 4개 작업/링크와 경로 보완 후 9개 작업/전체 링크 종료 0
(`Saved/Logs/DungeonConsumeHttpRestartBuild-PathFix-20261020.log`). 백엔드 fixture Release
빌드는 경고/오류 0개. 첫 메모리 실행은 `-UserDir`에 따른 저장 경로 계산 차이로 실패해
공용 경로를 프로젝트의 고정 `Saved/Tests` 기준으로 보완했다. 실패 로그는
`Saved/Tests/DungeonConsumeHttpRestart/e14b3762670f48ae846a656b78baea36/Seed.log`에 보존한다.

보완 후 실제 PostgreSQL/HTTP와 UE 두 프로세스의 Seed/Resume가 모두 통과했다. proof:
`Saved/Tests/DungeonConsumeHttpRestart/da5b442bdc1b4fc1ab9c9381c47edcf1/proof.json`.
소비 1회·증거 조회 1회·단절 보고 2회, 소비 응답/단절 ACK 각각 1회 유실, journal 잔여 파일
0개였다. 실행 중 선택 소스 23개와 DLL 불변을 검증했으며 사후 소스 변경도 0개다. 다만
proof 작성 뒤 다른 작업의 DLL 재링크가 확인돼 이 결과를 그 후 DLL의 검증으로 재사용하지
않는다. 두 프로세스의 기존 기동 `Condition failed`/Error는 각각 15줄이며 로그/해시는 보존한다.

같은 backend fixture의 `--consumption-seal-only --postgres-only --require-postgres`도 13개
그룹/종료 0이었다(`postgres-seal.log`, 위 실행 폴더). 기존 미검증이던 취소/해시·UUID 봉인,
실제 HTTP 권한/중첩 응답, INSERT 실패 롤백·새 repository 영수증, 행 잠금의 소비/취소 우선
경합·대기 중 만료·기록 부재 미확정 보존을 확인했다. 두 검사의 임시 스키마만 정리했고
기존 public 데이터는 유지했다. 전체 suite는 반복하지 않았다.

단절 관측은 fixture 주입이고 시간 가속 검사다. 실제 Iris 클라이언트/소켓 단절·10분 자연
경과·OS 강제 종료·배포 서버/Steam·최종 지급은 검증하지 않았다. 미소비 티켓의 취소/봉인
응답 유실을 포함한 UE 재시작 복구는 아래에서 이어간다. 증거 없는 intent의 운영 복구와
2~4인 Iris 지급/퇴장 통합도 남는다.

## 미소비 티켓 봉인 응답 유실의 UE 재시작 복구 (2026-10-21)

기존 HTTP 재시작 러너에 `-CancelUnconsumed`를 추가했다. 운영 권한이나 강제 정리 API를
추가하지 않고 같은 UE 복구 코드·기존 조회/봉인 API를 세 프로세스로 연결한다.

- Seed: 테스트 호스트가 첫 소비를 endpoint 실행 전 503으로 거절한다. UE는 실제 소비
  결과를 알 수 없는 상태와 주입한 원래 peer의 단절 관측을 저장하고 종료한다. 조회 기록이
  없다는 사실만으로 미소비/취소를 확정하거나 가드를 해제하지 않는다.
- SealLost: 새 UE가 원래 UUID/hash/Steam/UTC를 복원해 조회→봉인을 호출한다. 실제 DB의
  티켓 삭제·취소 영수증 커밋 후 첫 200 응답만 유실시킨다. 해당 HTTP 실패 완료 뒤에도 원본
  journal과 미확정/입장·정산 보류가 남은 상태로 종료한다. fixture gate는 응답 유실 관측만
  전달하며 취소 증거 검증이나 가드 해제를 대신하지 않는다.
- Resume: 세 번째 UE는 이전의 메모리 취소 플래그·raw ticket 없이 같은 요청을 재조회/
  재봉인한다. 같은 취소 증거를 받은 뒤에만 원래 파일을 정리하고 보류를 해제한다. 복구 중
  입장 콜백·가짜 접속 세대·단절 보고는 없으며 파티/보상 자격은 유지한다.
- 취소 뒤 같은 UUID/다른 UUID/legacy 소비와 새 티켓에 봉인 UUID를 붙인 소비를 실제
  HTTP로 모두 거절한다. 반대로 새 티켓·새 UUID는 실제 세대 1로 소비되며 이 명시적 양성
  대조군 이전에는 세대를 만들지 않는다. 남은 파티원 상태와 양쪽 정산 대상도 보존한다.

관련 실제 결함도 수정했다. .NET의 100ns 시각을 PostgreSQL이 마이크로초로 저장하면서
최초 취소 응답과 이후 DB 영수증의 `cancelledAt`이 달라질 수 있었다. 7 tick 정밀도 검사로
실패를 재현했고 `PostgresGameRepository.JoinTicketSeal`이 INSERT의 `RETURNING cancelled_at`
값을 첫 응답에도 쓰도록 보완했다. 원자적 삭제/영수증 저장과 권한·행 잠금·불명확 시 보류는
유지하며 DDL 변경은 없다. 실패/수정 후 로그는 각각
`Saved/Logs/DungeonCancellationPrecision-Before-20261021.log`,
`Saved/Logs/DungeonCancellationPrecision-After-20261021.log`다. 수정본 PostgreSQL 봉인 집중
14개 그룹/종료 0으로 정밀도·롤백·새 repository 복원·소비/취소 경합·기록 부재 보존을 확인했다.

재현: `Scripts/Network/Run-DungeonConsumeHttpRestartTest.ps1 -Postgres -CancelUnconsumed -NoRestore -TimeoutSeconds 240`.
실제 PostgreSQL/HTTP의 Seed/SealLost/Resume 모두 통과했고 proof는
`Saved/Tests/DungeonConsumeHttpRestart/6e40002eec9f424c832594449c83735c/proof.json`이다.
복구용 소비 시도 1회(503)·증거 조회 2회·봉인 2회/응답 유실 1회·단절 보고 0회,
늦은 소비 4종 거절과 새 소비 양성 대조 1회, journal 잔여 파일 0개였다. 실행 중 DLL/선택
소스 26개 불변과 같은 봉인 본문/취소 시각·원본 파일 보존을 검증했다. 사후 소스 변경도
0개였으나 다른 작업의 DLL 재링크가 있어 이 결과를 그 후 DLL의 검증으로 표시하지 않는다.
UE 변경은 공유 빌드 뒤 자체 타깃이 up-to-date/종료 0이었으며 새 3단계 진입을 실행으로
확인했다(`Saved/Logs/DungeonCancelHttpRestartBuild-20261021.log`). 자체 신규 링크 완료로
표시하지 않는다. backend Release 빌드는 경고/오류 0개였다.

스킬의 격리/보존 경계대로 세 검사의 소유 검증된 임시 스키마만 정리했고 public 데이터는
유지했다. 각 UE의 기존 기동 `Condition failed`/Error 15줄은 남는다. 이 실행은 정상 종료한
별도 UE 자동화 프로세스·주입한 peer 단절이며 실제 소켓/Iris·OS 강제 종료·Steam·최종
아이템/재화 지급 증거는 아니다. 관련 없는 전체 suite와 이미 확인한 소비 승자 시나리오는
반복하지 않았다. 증거/티켓이 모두 없는 intent의 읽기 전용 운영 진단은 아래에서 이어간다.
실제 2~4인 Iris 단절/회수·지급 연결과 안전한 운영 재개 정책은 별도이며 증거 부재만으로
취소나 가드 해제를 허용하지 않는다.

## 미확정 입장 복구의 서버 로컬 읽기 전용 진단 (2026-10-21)

`UHttpWebManager::BuildConfiguredDungeonRecoveryDiagnostics`와 서버 로컬 콘솔 명령
`RPG.Dungeon.RecoveryStatus`를 추가했다. 기본 32건이며 정수 인자를 주면 1~64건으로
제한한다. 요청 UUID 순으로 출력하고 전체/생략 건수도 표시한다.

```text
RPG.Dungeon.RecoveryStatus
RPG.Dungeon.RecoveryStatus 8
```

- JSON은 입장/정산 보류 여부, 소비 journal/outbox의 초기화·복원 대기·건강 상태·알려진
  영속 기록 수·미저장 쓰기·원인 불명 실패와 진행 중 요청 수를 구분한다. 기록 수/건강
  상태는 마지막으로 알고 있는 메모리 값이며 이 조회가 디스크를 다시 검사하지 않는다.
- intent별 UUID/원래 관측 UTC·현재 scope 일치 여부·단계를 제공한다. 관측 저장 대기,
  첫 게임 월드 대기, 이전 scope, 소비/증거/봉인 전송·재시도, outbox 인계, 취소 파일 정리,
  메모리 intent가 분리된 영속 기록을 구분한다. 세대가 확인된 경우 문자열로 표시해 int64
  정밀도를 유지한다. 읽기 때문에 월드를 바인딩하거나 영속 기록을 재수화하지 않는다.
- 최근 **증거/봉인** 완료의 작업 종류·HTTP 상태·완료 UTC·결과만 메모리에 기록한다.
  전송 실패/HTTP 거절/유효하지 않은 응답/정확한 미확정 응답/검증된 소비·취소 증거를
  구분하며 이 완료 시각은 원래 단절 시각을 대신하지 않는다. 재기동 후에는 `not_checked`
  로 시작한다. 예전 요청 객체나 다른 scope의 완료가 기록을 덮지 못한다.
- `lastOperation=seal`, `lastOutcome=backend_unresolved`이면 `operatorReviewSuggested`
  를 표시한다. 이는 백엔드가 원래 요청을 해소하지 못했다는 운영 확인 권고일 뿐, 티켓/
  영수증이 없다고 확정하거나 취소·미커밋·새 입장을 승인하는 신호가 아니다. 일반 409나
  인증/전송 실패를 이 권고로 바꾸지 않는다.
- 티켓·서비스 토큰·Steam 원문·티켓 해시·HTTP 본문·backend URL/서버 문자열은 출력하지
  않는다. 세션/요청 ID는 GUID 형식으로만 출력한다. 메인 게임 스레드의 서버 runtime만
  읽을 수 있고 콘솔은 현재 소유 GI의 유효한 서버 게임 월드를 요구한다. 클라이언트/
  teardown/종료·잘못된 인자를 거절한다. HTTP/RCON/클라이언트 RPC나 reset/force-clear
  기능은 추가하지 않았다. Native API의 월드 생성 전 조회는 저장된 상태만 보여준다.

진단은 HTTP 발행·재시도/시각 변경·파일 열기/승격/삭제·보류 해제를 수행하지 않는다.
스킬의 원본/미확정 보존 경계를 유지하며 운영자가 자료를 대조할 수 있는 조회만 제공한다.
증거 없는 건의 자동 해제·새 UUID 재발급·운영 강제 복구는 계속 허용하지 않는다.

검증: 초기 native 테스트의 콘솔 탐색을 실제 엔진의 `FindConsoleObject`/`AsCommand`로
보완했다. 수정 테스트 컴파일 뒤 다른 검증 프로세스의 DLL 사용으로 LNK1104가 발생했으며
해당 프로세스를 종료하지 않았다. 정상 종료 뒤 자체 DLL 링크/타깃 metadata 2개 작업·
전체 타깃 종료 0이었다(`Saved/Logs/DungeonRecoveryDiagnosticsBuild-Ready-20261021.log`).
초기 컴파일/잠금 로그는 같은 이름의 `...Build-20261021.log`, `...Build-Final-20261021.log`다.
`-ConnectionRecoveryOnly -SkipSDKSetup -TimeoutSeconds 600` 집중 native 19개/러너 종료 0.
proof: `Saved/Tests/DungeonFlow/Automation-851de2c44a404996a6b491b17b2638a3/DungeonFlow.proof.json`.
실행 및 사후 확인 시 DLL/선택 소스 24개 일치, 기존 기동 `Condition failed`/Error 15줄이다.
신규 검사는 출력 상한/비밀정보 배제, 실제 파일/요청/재시도 시각 불변, 일반/정확한 409·
전송/권한 오류, 지연 콜백/배정 변경, 저장 실패·취소 후 삭제 실패, 분리된 영속 기록/
재기동 월드 대기·원인 불명 실패 보존 및 콘솔의 잘못된 인자/월드 거절을 확인했다.

HTTP 완료 주입과 격리 파일의 native 증거이며 실제 운영 콘솔/HTTP·DB·Iris·Shipping
통합은 아니다. 백엔드 API·권한·보상 판정은 바꾸지 않아 이전 PostgreSQL/프로세스 재시작
시나리오를 반복하지 않았다. 다음은 실제 다인 소켓 단절을 기존 보고·10분 만료 회수·
남은 파티 정산에 연결하는 단계다. 증거 없는 intent의 안전한 운영 재개 정책과 다중
프로세스 fencing도 별도다.

## 실제 클라이언트 강제 종료와 자연 10분 유예 통합 (2026-10-21)

`Run-DungeonMembershipBackendE2ETest.ps1 -VerifyRemainingPartySettlement
-VerifyUnexpectedDisconnect`를 추가했다. 명시적 포기/응답 유실 시나리오와 혼합할 수 없다.
기존 개발 인증·새 티켓·Iris 실제 입장과 보스 맵을 재사용하되, 러너가 보관한 해당 실행의
클라이언트 `Process` 핸들 하나만 강제 종료한다. 에디터/서버/다른 실행은 종료하지 않는다.

- 운영 clock·10분 정책·단절/만료 DB 행은 직접 바꾸지 않는다. 서버의 실제 소켓 정리→outbox/HTTP 보고를
  기다리고 독립 SQL 읽기로 원래 관측 UTC·정확한 접속 세대/단절 세대를 대조한다.
- 자연 10분 동안 5초 간격으로 원래 관측/멤버십/lease와 남은 멤버의 세대를 대조하며,
  만료 직전 표본도 요구한다. 만료 POST나 단절 보고를 러너에서 대신 발행하지 않는다.
- 서버 probe는 남은 Controller/Pawn/PlayerState/Connection과 보스/encounter를 계속
  대조한다. 운영 폴러의 정확한 불변 만료 캐시가 있어야 정산 게이트를 통과한다.
- DB/HTTP 만료 기록은 명시적 포기와 구분하며 대상 멤버·lease·티켓만 제거됐는지,
  만료 후 새 티켓/재참가 거절과 정산 후 영수증 불변을 확인한다. 기존 서버 GAS 처치→
  실제 정산·인벤토리 보상·결과 버튼·로비 복귀는 남은 파티만 수행하고 대상 지급은 0이어야 한다.
- 테스트 프로세스/전투 제한만 opt-in 모드에서 1,200초로 늘린다. 운영/BP 타이머·유예는
  변경하지 않으며 Shipping에서는 probe가 활성화되지 않는다. 보상 고정 기대값은 현재
  authored Hard(골드 500·물약 3·투구 1)와 맞춰 세션 생성 시 Hard를 명시한다.

초기 로그 판정 회귀 27건과 후속 정상 복귀 경계 포함 32건이 통과했다. 실제 실행의 원본
러너 결과와 사후 검증은 아래처럼 구분하며, 수정 러너의 새 전체 PASS로 승격하지 않는다.
예기치 않은 종료를 명시적 포기 영수증으로 대체하지 않는 경계와 남은 파티 보존은
`project-rpg-online` 스킬을 따른다. Steam·다른 PC·자연 전투·유예 내 재접속·입장 대기 중
종료·서버 전체 장애·실제 패키징은 별도다. 이미 죽은 클라이언트의 로컬 실패/복귀나
살아 있는 만료 소켓의 강제 퇴장을 이번 시나리오로 증명하지 않는다.

첫 실행 `Saved/Tests/DungeonMembershipBackend/1d46bac6427949bbbe1f771d4053d4fc/`는
입장 전 `dungeon_connection_reports_pending`에서 중단됐다. 깊은 테스트 UserDir 때문에
부모 journal 경로는 238자, `ConsumeRecoveries` 디렉터리는 256자였다. UE의 Windows
경로 접두 처리는 260자부터지만 `CreateDirectoryW`는 더 짧은 디렉터리에서도 실패할 수
있어 자식 생성이 거절됐다. 운영 저장 실패 가드가 정상적으로 입장을 막은 것이며
데이터/보류를 지우는 방식으로 우회하지 않았다. 실패 로그/격리 schema는 보존했다.

러너의 UserDir만 `Saved/Tests/DU/<run UUID 앞 12자리>/s`로 줄였다. 생성 전 기존 경로와의
충돌을 거절하고, 가장 긴 journal 임시 파일도 260자 미만인지 검사한다. 실제 UserDir는
proof에 남기며 로그/백엔드/증거는 원래 전체 UUID 폴더에 유지한다. 재실행의 자식 경로는
204자로 정상 생성됐고, 실제 2인 입장→테스트 클라이언트 강제 종료→서버 소켓 정리·DB
단절 기록까지 확인했다. Editor 자체 타깃은 공유 컴파일 뒤 9개 작업/DLL 링크 종료 0,
backend 자체 빌드 경고/오류 0이었다.

### 실제 2인 자연 만료·정산 관측과 러너 보완

재실행 폴더: `Saved/Tests/DungeonMembershipBackend/56a675777f144825b870d1ab087ae5e1/`.
실제 관측 UTC `16:21:15.603` → 만료 `16:31:15.603`으로 정확히 10분이며 운영 폴러의
처리 시각은 `16:31:17.922253`이었다. 단절자의 멤버/티켓만 제거되고 명시적 포기 기록은
0이었다. 서버의 불변 만료 캐시→남은 동일 인스턴스의 GAS 처치/승리→정산 `Completed`
수령인 정확히 남은 1명→골드 500·가방 물약 3/투구 1→실제 결과 버튼/로비 맵까지 관측했다.
단절자 골드는 0이고 남은 아이템은 생성 때 지급한 대검 1개뿐이었다. 두 캐릭터의 대검은
`CharacterCreation`·장착 슬롯 9·Revision 1이며 생성 시각이 단절 전이고 수정 시각도 같다.
outbox ACK 후 journal 파일 0도 확인했다.

원본 러너는 마지막 보상 검사에서 기본 장비를 추가 보상으로 오판하여 **exit 1**이었다.
`Failed.proof.json`은 그대로 보존했다. 새 러너는 입장 전 실제 HTTP의 기본 장비/골드 0
스냅샷을 저장하고 같은 InstanceId의 전체 장비 레코드가 변하지 않았는지 대조한 뒤 나머지
아이템만 보상으로 계산한다. SQL 총량도 단절자 1개/수량 1, 생존자 3개/수량 5로 바로잡았다.
실패 proof에도 완료한 단절 증거/닫힌 로그 해시를 남기도록 보완했다.

기존 파서는 정상 서버 측 로비 복귀의 `ConnectionLost`도 중도 퇴장으로 오판했다.
동일 PlayerId의 연결 종료가 결과 클릭과 실제 로비 도착 사이에 정확히 한 번 있는 경우만
복귀 단계 종료로 구분한다. 전투 중/클릭 전·로비 이후·다른 신원·중복·다른 실패 유형은
거절한다. 신규/기존 파서 회귀 총 32건 exit 0이며 C++ 동작을 바꿔 우회하지 않았다.

다음 읽기 전용 재분석은 새 UE/백엔드 실행·시간 변경·DB 쓰기 없이 닫힌 로그와 해당
격리 schema의 SQL만 대조했고 **exit 0**이었다.

```powershell
& .\Scripts\Network\Review-DungeonUnexpectedDisconnectEvidence.ps1 `
  -RunId 56a675777f144825b870d1ab087ae5e1
```

`ReadOnlyReview.proof.json`은 `analysisPassed=true`, `originalRunnerPassed=false`,
`runnerRerun=false`로 구분한다. 원본 선택 입력 43개 중 사후 변경은 러너/파서 2개이며
C++/실행 DLL은 일치했고 닫힌 로그 5개 해시를 남겼다. 최초 세대/유예 직전 표본은 실행
중 검사했으나 구형 실패 proof에는 별도 보존되지 않았고, 입장 전 장비 스냅샷도 없어 DB의
생성 출처/시각·미변경 Revision으로 구분한 사후 증거임을 명시했다. 수정 러너 전체의
새 종료 0이나 전체 native 회귀를 주장하지 않으며 같은 10분 시나리오는 반복하지 않았다.
이 실행의 Condition failed는 0, 서버/단절 클라이언트 Error는 0이고 생존 클라이언트에는
정상 복귀 때의 엔진 ConnectionLost Error 1개가 있다. 프로젝트 전체 무오류 증거는 아니다.

두 실행의 소유 UE/백엔드 프로세스만 종료했고 기존 프로세스/DB public 데이터와 격리
schema/로그는 보존했다. 다음은 유예 내 실제 재접속의 새 세대 확인으로 단절 시각이
해제되는 흐름과 늦은 이전 연결 보고가 복구를 덮지 않는 경계다. 실제 4인·입장 중 종료·
서버 전체 장애·Steam·패키징은 계속 별도다.

### 자연 만료의 HUD 전용 관측 후속 (2026-10-28)

`-ObservePartyCombatHud -ObservePartyCombatExpiry`를 기존 자연 단절/파티 fixture에
추가하면 유예 중·만료 직전 행 유지와 확정 만료 후 행 제거까지만 관측하고 승리/정산/
복귀는 실행하지 않는다. 600초 정책·운영 만료 API/폴러는 유지한다. 실제 단절부터 정확히
600초·DB 표본 110개와 화면 5개를 관측했지만 공용 Controller의 동시 소스 변경으로 최종
입력 가드에서 러너는 종료 1이었다. 기존 DLL 관측과 새 소스/전체 성공을 구분한 증거는
[파티 HUD 기록](DUNGEON_PARTY_COMBAT_HUD.md#자연-10분-만료의-실제-hud-관측-2026-10-28-공유-소스-변경으로-전체-러너는-실패)을 따른다.
앞선 남은 파티 정산/복귀 러너의 실패를 이번 관측으로 성공 처리하지 않는다.

## 유예 내 실제 재접속과 이전 세대 차단 (2026-10-21)

기존 실제 단절 러너에 `-VerifyReconnectWithinGrace`를 추가했다.

```powershell
& .\Scripts\Network\Run-DungeonMembershipBackendE2ETest.ps1 `
  -VerifyRemainingPartySettlement -VerifyUnexpectedDisconnect -VerifyReconnectWithinGrace `
  -ClientCount 2 -TimeoutSeconds 600 -SkipPostgresStart
```

- 최초 입장/확인 완료→해당 실행의 클라이언트 하나만 강제 종료→실제 서버 소켓 정리와
  영속 단절 보고를 재사용한다. 최초 세대/관측 UTC/보고 전후 멤버를 즉시 별도 proof에
  남겨 후속 검사 실패 때도 관측 증거를 잃지 않는다.
- 같은 개발 계정·캐릭터의 새 UE 프로세스가 실제 로그인/세션 재개/새 티켓/입장을 한다.
  새 세대의 입장 확인은 운영 UE 서버만 발행하고 러너가 대신 확인하거나 DB를 고치지 않는다.
  원래 10분 유예 안에 세대가 정확히 1 증가·확인 세대 일치·단절 시각 해제를 요구한다.
- 서버 probe는 남은 원래 Controller/Pawn/PlayerState/Connection·세대와 보스/encounter를
  계속 대조한다. 재접속자는 실제 새 연결/신원·세대/가방 로딩이 준비돼야 정산 게이트를 연다.
- 이후 러너가 이전 세대의 단절 보고와 입장 확인을 각각 한 번 보낸다. 현재 HTTP 계약의
  403 거절 뒤 멤버/세대/단절 시각·전체 lease/영수증을 다시 읽어 복구를 덮지 않았는지 확인한다.
  이는 명시적인 오래된 요청 probe이며 현재 세대 단절/확인이나 만료 POST를 합성하지 않는다.
- 기존 서버 GAS 처치→전체 파티 정산/각 보상→각 결과 버튼/로비 복귀를 연결한다. 끊겼던
  캐릭터도 기본 대검을 유지하고 정상 보상을 받아야 한다. 원래 죽은 프로세스와 새 프로세스의
  로그/PlayerId는 분리하며 다른 신원/세션·중복 로그·세대 정밀도 손실을 허용하지 않는다.

스킬의 원래 관측/미확정 보존·남은 파티 보존 경계를 유지하며 운영 유예·배정·보상 권한과
DB 스키마는 바꾸지 않는다. 로그 판정 집중 17건(2/4인 합성 로그·2^53 초과 int64 포함)은
통과했다. 실제 UE/HTTP/DB 실행은 별도 후속 결과로 구분한다. 첫 Editor 빌드는 동시에
수정된 NPC 헤더의 UCLASS/GENERATED_BODY 줄 번호와 UHT 생성 파일 불일치로 exit 6이었고
`Saved/Logs/DungeonReconnectBuild-20261021.log`에 보존했다. 다른 소스를 되돌리지 않았다.
Steam·4인 실접속·10분 정각 재접속·입장 확인 응답 유실·서버 전체 장애·자연 전투는 별도다.

### 실제 2인 재접속·전체 파티 보상 결과

헤더 정합성이 회복된 후 새 테스트 로그 분기의 `UE_LOG`/else 괄호 오류(C2181)를 수정했다.
실패는 `DungeonReconnectBuild-Resync-20261021.log`에 보존했고 최종 자체 Editor 타깃은
`RPGDungeonFlowE2EProbe.cpp` 컴파일·게임 DLL 링크·metadata 총 4개 작업/exit 0이었다
(`Saved/Logs/DungeonReconnectBuild-Final-20261021.log`). 전체 native 회귀는 반복하지 않았다.

기존 Docker Desktop이 꺼져 있어 설치된 앱만 숨김 기동했고 기존 PostgreSQL 컨테이너의
정상 상태를 확인했다. 컨테이너/볼륨을 지우거나 기존 데이터를 초기화하지 않았다. 실통합
백엔드는 별도 출력 폴더/격리 schema를 사용해 빌드 경고·오류 0이었다.

위 명령에 `-NoBackendRestore -Port 18858 -BackendUrl http://127.0.0.1:3058`을 더한 실제
2인 실행이 **러너 exit 0**으로 통과했다. proof:
`Saved/Tests/DungeonMembershipBackend/cb79cea645834a019e09f9402e7634f0/DungeonReconnectWithinGrace.proof.json`.

- 원래 프로세스 강제 종료→실제 소켓 정리/PG 단절 관측→약 33.19초 안에 같은 캐릭터가
  새 UE 프로세스로 실제 티켓 입장했다. 세대 1→2, 확인 세대 2, 단절 시각 해제를 확인했다.
  남은 플레이어의 원래 연결/액터/세대와 보스 encounter는 유지됐다.
- 이전 세대의 단절/입장 확인 POST는 각각 실제 403이었다. 이후 3회 SQL 표본에서 복구된
  전체 멤버십·세대/단절 해제·lease가 유지됐고 명시적 포기/단절 만료 영수증은 0이었다.
- 실제 서버 GAS 처치→`Completed` 정산 수령인 정확히 2명→각 골드 500·가방 물약 3/투구 1,
  우편 0→두 현재 클라이언트의 결과 버튼/실제 로비 맵 복귀를 확인했다. 기본 장비의 전체
  HTTP 레코드는 입장 전 스냅샷과 같고, DB의 각 총 아이템 3개/수량 5도 일치했다.
- 실행 및 사후 선택 입력 46개·닫힌 로그 6개/DLL 해시 일치, 흐름 실패/Condition failed는
  모두 0이었다. 서버/죽은 클라이언트 Error 0, 두 정상 복귀 클라이언트는 각각 예상된
  `ConnectionLost` Error 1개로 프로젝트 전체 무오류 증거는 아니다.

관측/초기 장비 proof와 격리 schema는 보존했고 해당 실행의 프로세스만 종료했다. 앞선
자연 10분 만료 러너의 실패 proof를 덮거나 그 보완본을 새 전체 PASS로 승격하지 않았다.
이번 재접속 모드와 공통 기본 장비/복귀 경계가 새 실행으로 검증된 것이며, 자연 10분이나
기존 광역 회귀를 다시 실행하지 않았다. 복구 후 원래 만료 시각까지 실제로 기다린 증거,
같은 프로세스 자동 재접속 UI·실제 Steam·입장 확인 대기 중 재단절/응답 유실은 별도다.
다음은 새 연결의 입장 확인 대기 중 다시 끊기는 경우의 원래 관측/보상 자격 보존 경계다.

## 재접속 입장 확인 전 재단절과 늦은 확인 거절 (2026-10-22)

`-VerifyReconnectAdmissionLoss`를 기존 재접속 모드에 더했다. 이번 범위는 확인 요청이
**DB 커밋 전** 보류된 동안 두 번째 소켓이 사라지는 경우다. 커밋한 200 응답만 유실되는
경우와 구분하며, 그 경우까지 최초 시각이 유지된다고 일반화하지 않는다.

```powershell
& .\Scripts\Network\Run-DungeonMembershipBackendE2ETest.ps1 `
  -VerifyRemainingPartySettlement -VerifyUnexpectedDisconnect -VerifyReconnectWithinGrace `
  -VerifyReconnectAdmissionLoss -ClientCount 2 -TimeoutSeconds 600 -SkipPostgresStart
```

테스트 전용 loopback 프록시는 배정 토큰 해시·세션·캐릭터·Steam·세대 2의 실제 UE 확인
요청만 메모리에 보류한다. 나머지 HTTP는 전달하며 티켓/인증 원문·본문은 파일/로그에 쓰지
않는다. 해시/신원 불일치는 upstream 전에 실패한다. 파일 게이트는 지연 전달 시점만 정하고
입장 권한을 만들지 않는다. 소켓 정리·실제 단절 보고를 확인한 뒤 캡처한 원래 요청/헤더/
본문을 전달하며, 러너가 현재 세대의 새 확인 요청을 합성하지 않는다.

서버 probe는 대기 맵의 해당 신원/세대를 읽어 Pawn/인증 신원이 없는지 확인하고, 정리 후
이를 활성화하지 않았는지 관측한다. 다른 파티원의 기존 연결·액터·세대와 같은 보스
encounter를 계속 대조한다. 운영 입장/보상/유예 판정·DB 스키마와 미확정 가드는 그대로다.
스킬의 최초 관측·남은 파티 보존 경계를 유지하며 강제 해제 기능은 추가하지 않았다.

실제 2인 PostgreSQL/HTTP/Iris에서 다음 상태 전이가 확인됐다.

| 단계 | 소비 세대 / 확인 세대 | 저장된 단절 시각 | 로컬 입장 |
| --- | --- | --- | --- |
| 최초 소켓 단절 | 1 / 1 | 최초 관측 | 정리 |
| 재접속 소비·확인 보류 | 2 / 1 | 같은 최초 관측 | 신원/Pawn 비활성 |
| 확인 대기 소켓 종료·늦은 확인 409 | 2 / 1 | 같은 최초 관측 | 미활성·정리 |
| 세 번째 연결의 실제 확인 | 3 / 3 | 해제 | 정상 활성 |

이후 이전 세대 요청은 403으로 거절되고 전체 멤버/lease가 유지됐다. 실제 서버 GAS 승리→
PG `Completed` 정산 수령인 정확히 2명→각 골드 500·가방 물약 3/투구 1·우편 0과 기본
대검 불변→두 현재 클라이언트의 결과 버튼/실제 로비 복귀까지 **러너 exit 0**이었다.
위 명령에 `-NoBackendRestore -Port 18868 -BackendUrl http://127.0.0.1:3068`을 더한 실행이다.
proof: `Saved/Tests/DungeonMembershipBackend/09644e0c696a473c9f24118f88404b3d/DungeonReconnectWithinGrace.proof.json`.
`admissionPendingSocketLoss=true`와 `unexpectedDisconnect.pendingAdmissionLoss`로 기존
단순 재접속 증거와 구분한다. 관측/초기 장비/확인 보류/재단절 proof도 같은 폴더에 보존했다.

검증: 자체 Editor 타깃 21개 작업/게임·옵션 DLL 링크 exit 0
(`Saved/Logs/DungeonPendingReconnectBuild-20261022.log`), backend 빌드 경고/오류 0,
프록시 단위 3건과 기존/신규 파서 집중 24건 exit 0. 실행 및 사후 첫 확인 때 선택 입력
49개·닫힌 로그 9개/DLL 해시가 일치했다. 문서 갱신 후 최종 확인에서는 공유 DLL 해시가
변경됐으므로 이후 바이너리까지 통과로 확장하지 않고 이력 증거로 보존한다. 소스/로그는
그대로이며 DLL 변경만으로 같은 시나리오를 반복하지 않았다. 프록시는 실제 1건 보류/
1건 전달/409/실패 없음이었다. 흐름 실패/
Condition failed는 모두 0이며 서버·죽은 두 클라이언트 Error 0, 정상 복귀 두 클라이언트는
각 엔진 `ConnectionLost` Error 1개로 전체 무오류 주장은 하지 않는다.

첫 준비 시도 `.../c88fa8a1cad24479b02c998b74209c2f/`는 UE 시작 전 빈 프록시 로그의 null
정규식 파싱으로 중단됐고 보존했다. 빈 로그는 파싱하지 않게 수정한 뒤 새 격리 schema로
실행했다. 기존 데이터/다른 프로세스는 보존하고 이 실행 소유 프로세스만 종료했으며,
자연 10분/이전 광역 회귀는 반복하지 않았다. 실제 Steam·4인·같은 프로세스 자동 재접속·
10분 정각·저장 실패·확인 커밋 뒤 응답 유실·서버 전체 장애는 별도다. 다음은 확인 커밋 뒤
200 응답이 유실된 재접속의 정확한 새 단절 시각/미확정 복구 경계다.

## 재접속 확인 커밋 뒤 응답 유실과 새 단절 시각 (2026-10-22)

기존 재접속 러너에 `-VerifyReconnectConfirmationResponseLoss`를 추가했다.
`-VerifyReconnectAdmissionLoss`를 함께 지정해야 하며, 기본 커밋 전 보류 모드는 유지한다.

```powershell
& .\Scripts\Network\Run-DungeonMembershipBackendE2ETest.ps1 `
  -VerifyRemainingPartySettlement -VerifyUnexpectedDisconnect -VerifyReconnectWithinGrace `
  -VerifyReconnectAdmissionLoss -VerifyReconnectConfirmationResponseLoss `
  -ClientCount 2 -TimeoutSeconds 600 -SkipPostgresStart -NoBackendRestore `
  -Port 18869 -BackendUrl http://127.0.0.1:3069
```

프록시의 `after-commit` 모드는 실제 UE의 정확한 배정/신원·세대 2 확인 요청을 그대로
전달한다. upstream의 전체 200/`admissionConfirmed=true`를 검증한 뒤 응답 헤더/본문을
UE에 전달하지 않고 보류한다. 독립 SQL로 커밋을 확인하고 소유 대기 클라이언트만 강제
종료한 뒤, 실제 단절 보고/소켓 정리를 기다려 보류 응답을 버린다. 프록시는 토큰/Steam/
요청·응답 본문을 파일/로그에 기록하지 않으며 러너가 성공 확인 요청·단절 시각·권한을
합성하지 않는다.

이번 경계는 커밋 전 재단절과 다르다. **확인 커밋이 이미 이전 단절을 해제했으므로 새
실제 단절 시각을 저장한다.** 최초 단절 시각으로 되돌리거나 확인 세대를 낮추지 않는다.

| 단계 | 소비 / 확인 / 단절 세대 | 저장된 단절 시각 | 로컬 입장 |
| --- | --- | --- | --- |
| 최초 소켓 단절 | 1 / 1 / 1 | 최초 관측 | 정리 |
| 재접속 확인 커밋·응답 보류 | 2 / 2 / 0 | 없음 | 신원/Pawn 비활성 |
| 대기 소켓 종료·200 응답 유실 | 2 / 2 / 2 | 새 실제 관측 | 미활성·정리 |
| 다음 연결의 실제 확인 | 3 / 3 / 0 | 없음 | 정상 활성 |

실제 2인 HTTP/PostgreSQL/Iris 러너 **exit 0**:
`Saved/Tests/DungeonMembershipBackend/e9dc5efefc78437d8e34c65a041fdb62/DungeonReconnectWithinGrace.proof.json`.
`confirmationResponseLossRequested=true`, `unexpectedDisconnect.pendingAdmissionLoss.faultMode`
`after-commit`과 별도 `confirmation-hold.proof.json`/`PendingReconnectLoss.proof.json`으로
범위를 구분한다. 최초 UTC `16:15:42.823`, 새 클라이언트 종료 UTC `16:16:16.8233957`,
새 단절 UTC `16:16:16.985`(모두 2026-10-21 UTC)이며 새 시각+600초를 유효 기한으로
검사했다. 실제 프록시는 확인 1건/커밋 200 1건/응답 폐기 1건, 실패 없음이었다.

응답 폐기 뒤 3회 독립 SQL에서 세대 2의 새 시각·확인 세대 2/나머지 멤버·lease가
유지됐다. 세대 3의 실제 입장으로 단절이 해제되고 이전 세대 요청은 403으로 거절됐다.
같은 남은 연결/액터·보스 encounter, PG `Completed` 수령인 정확히 2명, 각 골드 500·
가방 물약 3/투구 1·우편 0·기본 대검 불변과 두 결과 버튼/실제 로비 복귀를 확인했다.

프록시 단위 6건·로그 파서 26건 및 변경 PowerShell 구문 검사가 통과했다. 단위 테스트의
200→409 반복 요청은 stub이며 이번 실제 UE 실행에서는 세대 2 재전송이 없었으므로
그 반복 경계를 실제 DB에서 검증했다고 계산하지 않는다. C++/운영 권한·유예/정산 정책은
바꾸지 않았고 기존 probe가 포함된 공유 DLL로 실행했다. UE 재빌드/신규 링크나 native
전체 회귀는 하지 않았다. backend 실행별 사본 빌드는 경고/오류 0이며 실행 후 DLL·선택
입력 49개·닫힌 로그 9개의 해시가 첫 사후 확인까지 일치했다. 문서 정리 중 다른 작업이
`RPGPlayerController.cpp`/`.h`를 갱신한 뒤 최종 확인에서는 선택 소스 2개가 달라졌다.
DLL과 닫힌 로그 9개는 그대로이며 후속 소스까지 통과로 확대하지 않고 변경도 보존했다.
이 이유만으로 재빌드/같은 시나리오를 반복하지 않았다. 의도한 응답 유실의 서버 libcurl 65 Warning과
정상 복귀 두 클라이언트의 `LogNet: Error` 각 1개는 남아 전체 무오류로 표시하지 않는다.

실행 소유 프로세스만 정리하고 고유 schema/증거와 기존 DB/다른 프로세스를 보존했다.
자연 10분/이전 광역 회귀는 반복하지 않았다. 실제 4인·같은 프로세스 자동 재접속·Steam/
Shipping·서버 전체 장애·관측 저장 전 장애/다중 프로세스 fencing은 별도다. 다음 기능
작업은 로드맵 P1의 게임 중 포기 확인/미확정 결과 UX의 개발 전용 권한 경계다. 남은
통합 검증 항목을 완료로 올리거나 이 결과만으로 운영 게임 중 포기 UI를 공개하지 않는다.

## 입장권 목적지의 배정 주소 대조 (2026-11-01, 구현·개별 컴파일)

`ConnectToDungeon`의 주소 인자는 목적지 변경 권한이 아니라 현재 인증된 세션 주소에
대한 확인이다. 호출자/백엔드 주소의 바깥 공백만 제거하고 정확히 일치하는 경우에만 기존
Join Ticket 요청으로 진행한다. 다른 호스트/포트·맵 경로/추가 URL 옵션이나 빈 배정은
`dungeon_server_address_mismatch`로 HTTP 요청 전에 거절한다. Steam 메타데이터를
이미 대조하는 상위 경로뿐 아니라 직접 Blueprint 진입에도 같은 경계를 적용한다.

같은 세션·캐릭터의 성공 티켓 응답도 이동 직전에 캡처한 주소와 최신 세션 주소의 일치,
배정 Server ID 존재, Loading/InProgress 및 선택 캐릭터의 현재 멤버십을 다시 확인한다.
불일치는 `dungeon_server_assignment_changed`로 busy 상태/로컬 pending 대상만 정리하고
이동/티켓 전달을 하지 않는다. 백엔드 스냅샷·계정/캐릭터·정산/보상은 바꾸지 않는다.
다른 세션/캐릭터의 응답은 기존대로 원래 요청을 완료시키지 않으며, 기존 만료/멤버 거절
코드와 재입장 복구 계약은 유지한다. 새 backend 조회나 서버 재배정 세대 프로토콜은 아니다.

현재 게임의 `ConnectWithJoinTicket` 호출자는 이 subsystem 하나임을 확인했다.
테스트 소스에는 다른 주소/포트·URL 옵션 거절, 응답 대기 중 주소/상태/멤버 상실과
정상 IPv4/IPv6/DNS 이동 예약 보존을 추가했다. 실행 직전 예약까지만 검증하도록 작성했고
실제 이동은 tick하지 않지만, **이번 턴에는 이 native 테스트를 실행하지 않았다**.

동시 정상 빌드 중 첫 입력 cohort가 바뀌어 컴파일/링크 전에 폐기했다. 반복 링크 대신
현재 헤더/PCH·소스2653개를 짧게 복사/전후 해시 대조한 닫힌 사본으로 구현/검사 CPP2를
개별 컴파일하여 각각 종료0을 확인했다. UHT/UBT·공유 Intermediate/정상 DLL 쓰기0,
private link·PIE/native·HTTP/DB/Steam 실행0이다. 기존 정상 DLL에 이 수정이 들어갔다고
간주하지 않으며 다음은 공유 자원 인계 후 정상 모듈 반영과 집중 테스트 실행이다.

두 임시 입력/OBJ/PCH 사본7,123,998,302 bytes(6.635GiB)는 자동 폐기·잔여/정리 예외0이다.
작은 실행기·명령/입력 해시·실패/최종 결과와 컴파일 로그만
`Saved/BuildChecks/DungeonTravelTarget-20261101/`에 남긴다. 폐기된 사본/OBJ 해시는 당시
컴파일 근거이며 같은 바이너리의 재실행 가능성이나 정상 모듈/실제 입장 성공을 뜻하지 않는다.

### 기존 정상 모듈의 반영 확인과 집중 실행 완료 (2026-11-01)

앞 절의 정상 반영/실행 대기는 해소했다. 이후 tooltip 담당의 정상 빌드 입력1344개 기록에서
구현/검사 CPP2가 앞 절/현재와 동일한 SHA임을 확인했고, 현재 정상 DLL도 해당 빌드의
`552DB6D7…0C421F19`와 일치했다. 중복 UBT/링크·원본 출력 교체 없이 그 모듈을 재사용했다.
이는 본인의 새 빌드나 현재 작성 중인 타 기능까지의 전체 소스 빌드 성공 주장이 아니다.

Config/모듈/플러그인 입력103개를 FileShare ReadWrite/Delete의 짧은 cohort 복사·전후
집합/해시 대조로 분리하고, 원본 게임 Source/루트 Content는 실행 입력에서 제외했다.
플러그인 Content/Resources만 읽기 junction으로 연결하고 종료 후 링크만 해제했다.
실제로 닫힌 사본 경로에서 같은 DLL의 로드를 관측했다. 두 집중 검사는 모두 경고/오류0
Success·UE 종료0이다.

- `ProjectRPG.Online.DungeonTravelTarget.Binding`: 다른 호스트/포트·맵/추가 옵션을
  HTTP 전에 거절하고 늦은 성공의 주소/서버 정보·상태/멤버 상실을 차단한다. 외부 응답은
  원래 요청을 완료하지 않으며, 일치한 IPv4/IPv6/DNS는 원래 주소+입장권의 이동 예약을 유지한다.
- `ProjectRPG.Online.DungeonJoinTicketFailure.Flow`: 기존 유예 만료/미활성 멤버의
  알려진 거절 코드와 신원/스냅샷 보존을 유지한다.

이 검사는 tick 전에 예약을 비워 실제 소켓/원격 이동을 실행하지 않는다. HTTP/DB/Steam·
실제 로비 버튼/PIE·네트워크 성공으로 확대하지 않는다. 기동 단계 Error15/검사 시작 이후0을
구분했다. 첫 실행은 테스트 시작 전 플러그인 로딩이240초를 넘어 자기 프로세스를 종료했고,
기동 한도만600초로 늘린 재실행에서 두 검사를 처음 완료했다. 제품/fixture 수정과
이미 성공한 검사의 반복·공유 빌드는 없으며 첫 실패도 따로 보존했다.

선택 Source2·닫힌 사본103개·정상 모듈은 검사 종료까지 동일했다. 두 자기 runtime/DLL/
UserDir/캐시 사본237,601,285 bytes(226.59MiB)와 junction20개는 종료/사용 해제 뒤 자동
폐기·잔여 UE/Temp·정리 예외0이다. 원본 Binaries/Intermediate·에셋·DB/공유 DDC는
정리 대상이 아니며 다른 담당의 작성/프로세스를 종료하거나 수정하지 않았다.
작은 실행기·기동 실패/최종 로그·명령/해시/결과만
`Saved/BuildChecks/DungeonTravelInstalled-20261101/`에 남긴다. 삭제 사본의 모듈 해시는
당시 로드 증거이며 같은 폐기 DLL의 재실행 가능성을 뜻하지 않는다.
