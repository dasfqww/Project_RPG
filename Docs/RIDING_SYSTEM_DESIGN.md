# Riding System 상세 설계 v1 — 로스트아크 벤치마크

- 작성·상세화: 2026-11-06
- 상태: **M1 상태/이동·기본 외형/구역과 M2 전투 입력/소모품·피해/제어 결과, Dash 서버 승인·실제 RMS/지형 제한·GAS 지급/활성화/HUD 클릭·기존 회피 키 입력·RMS 실행 식별/재생 보호·소유자 승인 프로필/GAS 키·LocalPredicted 실행/intent/취소 연결 구현 / 단위별 검증은 코드 설계14~26절 / 제품 보유 API·Emote·맵 배치/최종 자세·다인 네트워크 검증 후속**
- 담당: `riding-detail-20261106`
- 대체 문서: 기존 `MOUNT_SYSTEM_DESIGN.md` 초안. 그 경로는 이 문서로 안내한다.
- 구현 구조: [Riding System 아키텍처](RIDING_SYSTEM_ARCHITECTURE.md) — 클래스 책임·의존 방향·수명·기존 코드 연결·구현 분할. 게임 규칙과 저장 계약은 본 문서를 기준으로 한다.
- 코드와 검증 현황: [M1a 첫 기반 구현](RIDING_SYSTEM_CODE_DESIGN.md#10-m1a-첫-기반-구현--2026-11-06), [서버 세션 내부 로직](RIDING_SYSTEM_CODE_DESIGN.md#11-m1a-서버-세션-내부-로직--2026-11-06), [Actor 연결](RIDING_SYSTEM_CODE_DESIGN.md#12-m1a-actor-연결--2026-11-07), [기본 외형](RIDING_SYSTEM_CODE_DESIGN.md#13-m1b-기본-외형과-비동기-수명--2026-11-08), [CMC 이동과 예측](RIDING_SYSTEM_CODE_DESIGN.md#14-m1b-cmc-이동과-예측--2026-11-08). 이 문서의 M1 전체 및 M2~M5 완료와 구분한다.

이 문서는 사용자 요청인 로아식 탈것의 상세 설계다. 아래 “프로젝트 결정”은 구현을 진행할 수 있도록 이번 설계에서 선택한 기본 정책이며, 사용자가 각각을 별도로 확정했거나 로아의 모든 규칙과 같다는 뜻은 아니다. 수치 조정과 최종 에셋 저작은 플레이 검증에서 수행하되 상태·권한·저장 계약을 미정으로 남기지 않는다.

## 1. 범위·용어·벤치마크

**Riding**은 탑승/하차 및 탑승 중 플레이 기능, **Mount**는 수집하는 탈것 종류/데이터다.
새 동작 클래스·기능 경로·태그에는 Riding을, 정의 ID와 외형 데이터에는 Mount를 쓴다.
기존 플레이어 Pawn과 ASC를 유지한다. 탈것 외형은 독립 조종 Pawn이 아니다.

현재 로비는 등록·설정·보관함을 제공하는 프런트엔드 문맥으로 취급한다. 실제 탑승/이동은 확정 입장된 UE 권위 서버의 허용 구역에서 제공한다.
로비 미리보기 모델을 실제 탑승 상태로 취급하지 않으며, 새로운 온라인 허브 서버를 이 기능에 끼워 넣지 않는다.

v1 제공 범위:
- 등록 아이템 사용 → 원정대 공유 보유 → 보관함/전용 퀵슬롯 탑승.
- 즉시 탑승·하차·교체, 지상 이동, 공통 돌진 1종과 탈것별 외형/전용 모션.
- 캐릭터별 선택·즐겨찾기·무작위 즐겨찾기 호출, 탑승 전용 입력/HUD.
- 서버 권위·Iris 복제·이동 예측·실제 PostgreSQL 등록/재시도/재접속.
- 두 번째 탈것을 데이터/에셋 추가만으로 연결하는 저작 경로.

다인승, 탈것 HP·독립 AI, 기마전투, 자유 비행/수영, 탈것 성장/거래/대여·기간제 소유는 v1 범위 밖이다.
탈것 등록 전 아이템의 거래 가능 여부는 기존 아이템 정책을 따른다. 해금된 보유 권한은 거래하지 않는다.

| 근거 | 확인된 사실 | 프로젝트 적용 |
|---|---|---|
| [한국 공식 2026-11-04 업데이트](https://lostark.game.onstove.com/News/Notice/Views/13585) | 소환 모션 없이 즉시 탑승, 교체해도 이동기 남은 쿨타임 유지 | 즉시 상태 전환, 공통 이동기 쿨타임 |
| [공식 대만 이동 수단 가이드](https://m-lostark-event.mangot5.com/GameGuide/Pages/%E7%A7%BB%E5%8B%95%E5%B7%A5%E5%85%B7) | 아이템 등록/보관함/퀵슬롯, Space 이동기·일부 추가 액션, 감정표현, 즐겨찾기 무작위 호출 | 보유·전용 액션·보관함 흐름 |
| [Epic CMC 네트워크 이동](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine) | 저장 이동 재실행과 Root Motion Source를 통한 특수 이동 | 기존 CMC 확장, 이동 예측/보정 경로 사용 |

공식 자료만으로 로아의 모든 전투·피격·상호작용 세부를 확인하지 못했다. 아래 규칙은 그 공백을 메우는 **Project_RPG v1 결정**이다. 다른 서비스 지역의 제한이나 전투 세팅 제한을 한국 탈것 탑승 규칙으로 추론하지 않는다.

## 2. 게임 규칙 — 프로젝트 결정

### 2.1 탑승·하차·전투

현재 수동 능력 활성화/공격 입력은 [코드 설계16절](RIDING_SYSTEM_CODE_DESIGN.md#16-m2-수동-능력과-공격-입력-차단--2026-11-08), 소모품 준비와 탑승의 상호 배제는 [17절](RIDING_SYSTEM_CODE_DESIGN.md#17-m2-소모품-준비와-탑승의-상호-배제--2026-11-08), 적대 HP/보호막 피해 결과·10초 잠금은 [18절](RIDING_SYSTEM_CODE_DESIGN.md#18-m2-적대-피해-결과와-탑승-전투-잠금--2026-11-09), 승인 제어 성공 결과는 [19절](RIDING_SYSTEM_CODE_DESIGN.md#19-m2-승인-제어-성공과-탑승-전투-잠금--2026-11-09)의 구현·검증 범위를 따른다. Dash의 자기 창 취소와 외부 제어 이동 순서는 Dash 통합 후속이다. native GAS/Actor 검사와 실제 게임 제어/RMS/HTTP/PG·다인 실행을 구분한다.

| 상황 | v1 동작 |
|---|---|
| 정상 탑승 | 서버 승인 즉시 Mounted. 고정 소환 시전시간 없음. 소유 클라이언트는 승인 전 속도를 올리지 않는다. |
| 보유/정의/구역 정보 미준비 | 요청 거절. UI는 준비 중/사용 불가 이유를 표시하고 임의 기본 탈것을 지급하지 않는다. |
| 이동 중 탑승 | 지면 보행 중 허용. 방향/입력 유지, 기존 수평 속도에서 새 한도까지 정상 가속한다. |
| 전투 중 | 마지막 유효 적대 타격/피격 후 10초간 새 탑승·교체 금지. 단순 적 어그로만으로는 금지하지 않는다. |
| 유효 피격 | HP 또는 보호막에 실제 피해가 적용되거나 제어 효과가 성공하면 즉시 강제 하차. 피해/제어를 취소하거나 면역을 주지 않는다. |
| 지속 피해/잔존 공격 | 서버가 승인한 각 유효 적대 피해에 전투 잠금을 갱신. 탑승자의 잔존 공격이 명중해도 하차한다. 완전 면역/무효 판정은 갱신하지 않는다. |
| 시전/홀딩/차지/상호작용 중 | 탑승·교체 거절. 자동으로 기존 행동을 취소하고 올라타지 않는다. |
| 탑승 중 일반 공격 입력 | 하차만 수행. 같은 입력을 공격으로 재생/예약하지 않는다. 다음 새 입력부터 공격한다. |
| 탑승 중 전투 스킬/소모품 | 서버 활성화/사용 거절. 진행 중인 수동 전투 행동과 탑승의 동시 시작을 막는다. 기존 패시브·버프/디버프는 유지한다. |
| 수동 하차 | 지면·공중 모두 허용. 돌진/전용 모션 취소 후 현재 캡슐 위치에서 도보 상태로 전환. |
| 이동기 중 교체/재탑승 | Busy 거절. 하차·사망·피격·구역 금지는 즉시 우선 처리한다. |
| 같은 탈것으로 탑승 요청 | 이미 탑승한 경우 NoChange. 재시도로 하차되지 않는 명시적 SetMounted 명령을 쓴다. |
| NPC/채집/장치 상호작용 | 대상의 원래 거리/권한 검사 후 하차 완료 시 원래 상호작용을 한 번 실행. 하차 중 대상/세대 변경 시 다시 검사하고 실패 시 실행하지 않는다. |
| 사망/기절/경직/넉백/공중제어 | 강제 하차 및 자기 액션 종료. 전투 컴포넌트가 소유한 제어·이동 모드는 복원하지 않는다. |
| 감속 | 기존 감속이 활성화돼 있으면 탑승/돌진 불가. 탑승 중 감속이 적용되면 하차하여 기존 도보 감속 계산을 적용한다. |
| 점프/낙하/수영/사다리 | v1은 지상 보행만. Space는 돌진이며 점프하지 않는다. 자발적 낭떠러지 진입은 막고, 외력 등으로 Falling/Swimming/Custom 이동 진입 시 하차한다. |
| 월드 이동/접속 종료/권한 폐기 | 도보로 종료, 자기 입력·외형·액션만 정리. 타인의 능력/보상/멤버십에는 영향 없음. 새 접속은 자동 재탑승하지 않는다. |

전투 잠금은 서버의 기존 승인된 피해/제어 경계에서 갱신하는 `RidingCombatLockUntil`이다.
새 범용 전투 시스템을 만들지 않는다. 적대 판정과 피해 승인 ID는 기존 전투 규칙을 사용하고 클라이언트 이벤트로 타이머를 만들지 않는다.
이미 더 강한 행동 금지/사망/게임플레이 폐기 판정이 있으면 그 판정이 우선한다.

### 2.2 지역 정책

실제 서버 Box 정책/이동 경계 구현과 항상 로드 저작 조건은 [코드 설계15절](RIDING_SYSTEM_CODE_DESIGN.md#15-서버-구역-판정과-이동-경계--2026-11-08)을 따른다. 아래 정책을 기존 맵 전체에 배치·검증한 상태는 아니다.

`URPGRidingWorldPolicy`의 기본값은 Deny다. 실제 인증된 플레이 맵의 이동 공간·던전 통로를 명시적으로 Allow로 저작한다.
보스 전투장/협소한 실내/기믹 공간은 `ARPGNoRidingVolume`으로 금지한다. 같은 위치에서는 **어느 하나의 Deny도 Allow보다 우선**한다.
특정 탈것만 허용하는 구역은 AllowMountTags 집합의 교집합으로 처리하고 빈 교집합은 Deny다.
정책 미로드·모순 설정은 Deny, 단순히 “던전 맵”이라는 이유로 전부 금지하지 않는다.

정책 판정은 서버의 실제 캡슐 위치/이동 경로를 사용한다. 입장 순간·매 이동의 swept 경로·구역 정책 변경을 검사한다.
고속 돌진이 한 프레임에 금지 구역을 건너뛰어도 최초 경계에서 취소·하차한다. BeginOverlap 이벤트만으로 보장하지 않는다.
발판 이동으로 금지 구역에 들어가도 다시 검사한다. 클라이언트의 구역 ID는 권한 자료가 아니다.

### 2.3 속도·액션 기본값

수치는 UE cm/s 기준의 초기 튜닝 값이며 로아 수치를 복제한 것이 아니다.

| 값 | v1 기본값 |
|---|---|
| 탑승 기본 속도 | 모든 탈것 600 cm/s. 현재 기본 도보 MaxWalkSpeed 400 cm/s와 비교해 검증 |
| 가속 / 제동 / 회전 | 2048 cm/s² / 2000 cm/s² / Yaw 500°/s |
| Dash | 최대 지상 경로 길이 900 cm, 시간 0.6초, 시작 방향 고정, 공통 쿨타임 10초 |
| Dash 방향 | 현재 이동 입력 방향, 입력이 없으면 캐릭터 전방. Z 제거·유한 벡터 정규화 |
| Dash 종료 | 벽/통과 불가 경계/낭떠러지/구역 금지에서 조기 종료. 남은 쿨타임 환불 없음 |
| Emote | 최대 3초, 이동/돌진/하차/피격으로 취소. 2초 공통 재사용 간격. 이동·피해·면역 없음 |
| 충돌 | 현재 플레이어 캡슐 42/96 cm를 v1에서 유지. 모델은 이 이동 크기에 맞춰 저작 |
| 전투 이동속도 보정 | 탑승에는 MoveSpeed/MoveSpeedPercent 및 직업 이속 버프를 곱하지 않음. 절대 이동 금지/외부 속도 상한은 항상 우선 |

탑승 중 일반 걷기는 CMC Walking/NavWalking을 쓴다. 기존 도보 계산은 도보에서 유지한다.
Dash의 시각적 점프/부유는 모델 연출이며 절벽·벽을 넘을 권한을 뜻하지 않는다.
v1 두 탈것은 성능이 같고 모션/외형만 다르다. 연속 도약·홀딩 부스트는 새로운 실행 타입을 추가하는 후속 기능이며 임의 BP Tick 이동으로 대체하지 않는다.

## 3. 실제 코드와 소유 경계

초기 읽기 기준 게시 HEAD는 `dbe7e392`이며 검토 중 인벤토리 플러그인 `339468f6`과 거래 예약 `512d9d11`의 게시가 반영됐다. 아래는 이 기존 경계와 맞춘 신규 Riding 설계이며, Riding 구현 완료 주장이 아니다.

| 기존 연결점 | 상세 설계에서 사용할 경계 |
|---|---|
| [RPGBaseCharacter](../Source/Project_RPG/Private/Character/RPGBaseCharacter.cpp) | Pawn이 ASC를 소유하고 자신을 Avatar로 초기화하는 구조 유지 |
| [RPGPlayerMovementComponent](../Source/Project_RPG/Public/Component/RPGPlayerMovementComponent.h) | 폐기된 게임플레이의 이동 거절 유지. Riding은 하위 이동 정책으로 추가 |
| [RPGCharacterMovementComponent](../Source/Project_RPG/Private/Component/RPGCharacterMovementComponent.cpp) | GetMaxSpeed/행동 제한·이동 재현에 동일 Riding 프로필 연결 |
| [CombatControl](../Source/Project_RPG/Public/Component/Combat/RPGCombatControlComponent.h) | 승인된 제어 결과·상태 변경 구독. 자기 외형 정리를 이유로 제어 GE를 제거하지 않음 |
| [RPGPlayerState](../Source/Project_RPG/Public/Player/RPGPlayerState.h) | 인증 CharacterId/RosterId/세션·ConnectionGeneration과 Pawn 밖 세션 상태 |
| [아이템 플러그인 경계](INVENTORY_PLUGIN.md) | 공통 아이템/저널/통신은 RPGInventoryRuntime. Riding/게임 전용 Fragment는 Project_RPG |
| [Item API V2](../Backend/ITEM_API_V2.md) | 권한·리비전·영수증·원자 Commit. 일반 게임 서버 Commit으로 보유 탈것 목록 쓰기 금지 |
| [거래 예약 계약](PLAYER_TRADING_DESIGN.md) | 사용자 잠금과 별개인 활성 자산 예약 검사. 등록이 소비 경로를 우회하지 않도록 연결 |
| [기존 입력 확장 계약](../Source/Project_RPG/Public/Input/RPGAdditionalInputProvider.h) | 현재 LocalPlayer/World에 귀속된 키 조회·변경·기본값 복원 |
| [HUD 모듈](PLAYER_HUD_MODULARIZATION.md) / [옵션 입력 소유](OPTIONS_UI_INPUT_OWNERSHIP.md) | HUD 수명·기존 스킬 편성·다른 창의 포커스 유지 |

### 3.1 신규 클래스와 책임

다음은 구현할 이름이다. 아직 존재하는 API처럼 호출하지 않는다.

| 구성 | 소유자와 책임 |
|---|---|
| `URPGMountDefinition : UPrimaryDataAsset` | Mount 종류/외형/액션 참조. PrimaryAssetType=Mount, 영속 ID는 별도 MountId |
| `URPGRidingPolicy` | 공통 수치·전투 잠금·구역/프로토콜 버전. 서버 시작/Experience 로드 때 검증·고정 |
| `URPGRidingSessionComponent` | PlayerState 소유. 승인된 보유 스냅샷, 공통 쿨타임, 전투 잠금, 요청 예산. Pawn 교체에 독립 |
| `URPGRidingComponent` | Player Pawn 소유. 상태 전이·검사 조율·복제 및 자기 grant/GE/RMS 핸들 수명 |
| `URPGRidingPresentationComponent` | 동일 Pawn의 로컬 표현. 서버 상태에서 모델/자세/모션 재구성, async 세대 검사 |
| `URPGRidingDashAbility`, `URPGRidingEmoteAbility` | 기존 ASC에서 실행. 각각 이동기/비전투 모션을 담당 |
| `URPGRidingUIManager` | LocalPlayerSubsystem. 보관함 조회·변경/미확정 요청 복구·UI ViewModel. 계정/캐릭터 변경 시 컨텍스트 교체 |
| `FRPGRidingInputProvider` | 현재 플레이어의 추가 입력/키 설정 어댑터. 데이터 저장/서버 권한 없음 |
| `IRidingReadStore`, `IRidingCommandStore` | 백엔드 조회와 원자 등록/설정 쓰기의 좁은 포트. 기존 IGameRepository 확장 없이 DI 연결 |

동작·전용 Ability·상태 타입은 `Source/Project_RPG/{Public,Private}/Riding/`,
UI는 `UI/Riding/`, 에셋은 `/Game/Blueprints/Riding/`에 둔다.
정의 이름이 Mount라고 별도 MountManager나 또 다른 이동 컴포넌트를 만들지 않는다.
백엔드는 `Riding/` 유스케이스/정의와 Data 저장 어댑터, Api endpoint 파일로 구성한다.

### 3.2 정의 데이터

`MountId`는 소문자 ASCII `[a-z0-9][a-z0-9._-]{0,63}`의 안정 ID다. 에셋 이름 변경으로 바뀌지 않는다.

| 정의 | 필수 필드 |
|---|---|
| MountDefinition | MountId, DefinitionVersion, DisplayName, Icon, Mesh, AnimClass, RiderPoseLayer, SaddleSocket, RiderOffset, RegionTags, DashPresentation, EmotePresentation |
| RidingPolicy v1 | ProtocolVersion, CatalogVersion/GameplayHash, RideSpeed=600, Acceleration=2048, Braking=2000, YawRate=500, DashDistance=900, DashDuration=0.6, DashCooldown=10, CombatLock=10 |
| 등록 아이템 매핑 | ItemDefinitionId+Version → MountId. 1회 소비량=1. 다른 소모/장착 Fragment와 동시 등록 금지 |
| 액션 표시 | ActionId(dash/emote/dismount), 아이콘/문구/모션/소리. 실행 타입·수치는 서버 정책에서 결정 |

v1은 최대 512개 정의, 즐겨찾기는 캐릭터당 32개다. 정의의 중복 ID/없는 소켓/잘못된 액션/NaN/음수/범위 초과를 저작 검사에서 거절한다.
CatalogVersion은 불변 배포 단위다. UE cooked 정책과 백엔드 등록 매핑에서 동일한 정규화 manifest의 GameplayHash를 사용한다.
에디터 exporter가 JSON manifest를 만들고 UE·백엔드 시작 시 검증한다. 호환되지 않는 클라이언트/서버는 Riding을 비활성화하고 이유를 표시한다.
세션 중 gameplay 값을 hot reload하지 않는다. 외형 로드 실패는 보유 권한이나 실제 속도를 바꾸지 않는다.

## 4. 상태 전이와 RPC 계약

### 4.1 상태의 단일 기준

- `Mode = OnFoot | Mounted`. 요청 대기/모델 로드는 UI 상태이며 별도 서버 탑승 모드가 아니다.
- `Action = None | Dash | Emote`. Mounted 상태 아래의 현재 액션이다.
- `RiderEpoch: Guid`: 인증된 PlayerState↔현재 Pawn 연결마다 새 값. 오래된 Pawn/RPC/로드 완료를 차단한다.
- `StateRevision: uint32`: 탑승·하차·교체마다 증가. 넘치기 전 새 Epoch로 재초기화한다.
- `ActionSequence: uint32`: 시작/종료/취소마다 증가. 같은 Epoch의 상태 비교는 (StateRevision, ActionSequence) 순서다.
- `MountId/DefinitionVersion`: Mounted일 때만 유효. OnFoot는 빈 ID.
- `ExecutionId`, 액션 시작 서버 시각/길이/방향/취소 이유: 현재 액션의 표현·정리 기준.

`FRPGRidingRepState`를 하나의 복제 구조로 공개한다. OwnerOnly 요청 결과/쿨타임/보유 스냅샷은 별도다.
`State.Riding.Mounted` 태그/자기 Infinite GE는 이 상태의 투영이며 다른 정답이 아니다.
Riding 능력은 ASC 준비 시 자기 AbilitySet으로 1회 지급하고 Mounted 조건을 검사한다. 탑승할 때마다 재지급하지 않는다.
Pawn 종료/ASC 교체 시 자기 grant·GE·RMS·delegate 핸들만 정리한다.

### 4.2 명령과 결과

| 경로 | 입력 | 처리 |
|---|---|---|
| ServerSetRiding (Reliable) | Epoch, RequestSeq, ExpectedStateRevision, DesiredMounted, Selector | 보유·권한·정의·지면·행동·전투·구역 검사 후 원자 전환 |
| Selector | Specific(MountId) / Selected / RandomFavorites | ID/후보는 서버 스냅샷에서 검증. 클라이언트 후보 목록/확률은 받지 않음 |
| ClientRidingResult (Reliable) | RequestSeq, ResultCode, 현재 RepState, CooldownView | UI 대기 해제/이유 표시. 더 오래된 결과로 현재 상태를 되돌리지 않음 |
| Dash/Emote | 기존 GAS 활성화 + Epoch/StateRevision/ActionId/유한 XY 방향 | 기존 ASC ingress 및 추가 Riding 예산·자격 검사. arbitrary gameplay event로 우회 불가 |
| ForceDismount | 서버 내부 이유(enum) | 네트워크 명령을 만들지 않고 동일 종료 경로 사용 |

Selector는 DesiredMounted=true일 때만 있다. 토글 입력은 클라이언트가 명시적 목표 상태로 바꾼다.
성공/거절 enum: Applied, NoChange, NotOwned, NotReady, DefinitionMismatch, InCombat, ActionBlocked, WrongMovementMode,
RegionDenied, Busy, StaleState, RateLimited, AuthorityRevoked. 내부 계정/세션/자산 정보는 공개 오류에 넣지 않는다.

연결별 요청은 단조 RequestSeq를 쓰고 최근 32개 결과만 보관한다. 같은 Seq/동일 내용은 최초 결과 재전송, 내용이 다르면 거절.
보관 범위보다 오래된 Seq는 StaleState와 현재 상태만 반환한다. 다음 허용 Seq와 1024를 넘는 큰 점프는 거절한다.
동일 Epoch에서 관측한 Seq의 중복 여부와 고수위는 고정 크기로 관리하며 거절 요청도 예산을 소비한다.
기본 예산은 명령/액션 합산 burst 8, 초당 4개. 서버 단조 시계를 사용하고 과다 요청만으로 제재하지 않는다.
자세/피해를 매 Tick Reliable RPC로 보내지 않는다.

### 4.3 전이의 실행 순서

1. 현재 Pawn/PlayerState/ConnectionGeneration·폐기 여부와 RPC 소유자를 확인한다.
2. 요청 예산·Epoch/Seq·상태 리비전·스냅샷/정의·정책을 검사한다.
3. 성공할 전이와 자기 액션 취소/태그/프로필 변경을 준비한다. 서버 게임 스레드의 재진입 가드를 연다.
4. 새 상태 리비전을 먼저 확정하고, 취소/태그/이동 정책을 적용한다. 콜백 재진입은 확정된 세대를 확인한다.
5. 완료 상태를 복제·요청 결과로 알린다. 로컬 외형/HUD 갱신은 이 상태에서 수행한다.
6. 중간 실패는 준비한 자기 핸들만 정리하고 OnFoot로 수렴시킨다. 다른 시스템의 입력/제어를 이전 값으로 덮어쓰지 않는다.

강제 하차는 이미 OnFoot여도 안전한 멱등 정리다. 수동 하차와 피격이 같은 프레임이면 하차는 한 번만 확정한다.
상태 전환은 캡슐 좌표·크기를 변경하지 않으므로 옆으로 내릴 공간을 탐색하거나 순간이동할 필요가 없다.

## 5. 이동·예측·복제 상세

### 5.1 보행 프로필과 우선순위

최종 이동 판단 순서는 **권한 폐기/사망 → 행동 금지/외부 제어 → 외부 속도 상한 → Riding/도보 기본 계산**이다.
Mounted에서는 600에 기존 절대 상한을 적용하고 전투 MoveSpeed 배율을 제외한다.
예를 들어 외부 상한 300이면 300, 스턴이면 0이다. 외부 강제 이동의 힘/RootMotion 소유는 유지한다.

현재 CharacterMovement의 계산 경로에 Riding 값을 추가한다. 별도 Tick에서 MaxWalkSpeed를 반복 덮어쓰지 않는다.
가속/회전/제동도 현재 모드에서 유도하며 “탑승 전 값을 저장했다가 무조건 복구”하지 않는다.
탑승 도중 바뀐 다른 시스템의 제한을 하차 때 지우면 안 된다.
서버 보안 샘플은 같은 최종 프로필과 승인된 이동기 예산을 사용한다. 이동기만 기존 유한 거리/시간 승인 창을 열고 종료/취소 시 회수한다.

### 5.2 SavedMove·상태 전환

확장 후보는 기존 CMC의 `FSavedMove_Character`, `FCharacterNetworkMoveData`, `FCharacterMoveResponseData` 어댑터다.
새 엔진 fork 없이 프로젝트의 prediction data/move data/response container를 연결한다.

- SavedMove에 Epoch, StateRevision, 사용한 Riding profile 버전, 액션 prediction key/ExecutionId를 기록한다.
- 다른 상태 리비전·프로필·액션 시작/종료 경계를 넘는 move combine은 금지한다.
- Packed move는 상태 리비전과 액션 키를 전달하되, 서버가 속도/거리/태그를 클라이언트 값으로 설정하지 않는다.
- 서버는 수신 move를 **현재 서버 상태와 승인된 액션**으로 재현한다. 과거 리비전이 이동을 복원하거나 이미 하차한 Dash를 되살리지 않는다.
- 불일치면 일반 위치/속도 보정과 함께 현재 Riding state/profile·활성 액션 키를 move response에 싣는다.
- 소유 클라이언트는 보정 상태를 먼저 적용하고 미확인 move를 재실행한다. 오래된 리비전 move는 이동 입력만 재실행하고 Riding 프로필/취소된 RMS를 복원하지 않는다.
- 승인 전에는 도보 속도를 유지한다. 서버가 승인한 Mount/하차 시점과 이전 큐가 엇갈린 경우 보정을 허용하되 정상 지연을 부정행위로 처리하지 않는다.
- RepNotify와 move response가 역순으로 와도 현재 Epoch/리비전보다 오래된 프로필로 덮어쓰지 않는다.

프로필 변경은 bool 복제만으로 완료되지 않는다. R02/R03 네트워크 검증을 통과해야 M1/M2 완료다.

### 5.3 Dash 실행·예측 취소

현재 [서버 승인과 종료 수명](RIDING_SYSTEM_CODE_DESIGN.md#20-m2-dash-서버-승인과-종료-수명--2026-11-09)과 [실제 서버 RMS/보안 창·지형/거리 제한](RIDING_SYSTEM_CODE_DESIGN.md#21-m2-실제-서버-dash-이동과-자원-수명--2026-11-09)에 [LocalPredicted GAS/intent·소유자 RMS/거절·재생 정리](RIDING_SYSTEM_CODE_DESIGN.md#26-m2-dash-로컬-예측과-입력-intent--2026-11-11)를 연결했다. 단위 검증과 실제 NetDriver/다인/최종 맵 검증을 구분하며 제품 NotReady 경계를 유지한다.

Dash는 기존 GAS의 LocalPredicted 능력으로 구현한다.
클라이언트는 이미 승인된 Mounted 상태에서 입력 방향과 고정 정책으로 RMS를 예측 생성한다.
서버는 활성화/방향·상태/쿨타임을 검사하고 승인된 prediction key에 ExecutionId를 연결한다.
현재 [승인 프로필/GAS 키 구현](RIDING_SYSTEM_CODE_DESIGN.md#25-m2-dash-승인-프로필과-gas-활성화-키--2026-11-10)은 서버가 지급한 spec handle·탑승 세대·카탈로그 버전·거리/시간/쿨타임을 소유자 전용 불변 grant로 묶고, 실제 활성 GAS 키와 시작 전 ActionSequence에서 같은 실행 식별자를 계산한다. 프로필과 식별자만으로 권한이 생기지 않으며 현재 소유·활성 spec·서버 승인 조건을 다시 검사한다. 25절 당시에는 ServerOnly/서버 발급 키만 사용했다. 현재26절은 LocalPredicted/실제 client key·입력 intent와 owner RMS/거절·Engine ID 대응을 연결하며, 실제 패킷/다인 검증은 후속이다.
기존 GAS 예측/거절 경로를 사용하며 클라이언트에 새로운 능력 지급 권한을 주지 않는다.

실제 이동은 CMC의 Root Motion Source와 capsule sweep을 사용한다.
시작 위치는 서버의 현재 위치, 진행 방향은 검증된 XY 방향, 최대 누적 지상 경로는 900cm다.
서버는 클라이언트가 보낸 목적지를 사용하지 않는다. 낭떠러지/금지 구역/충돌 검사를 각 이동 구간에 적용하고 길이가 줄어도 부족분을 순간이동으로 보충하지 않는다.
통상 입력에 의한 추가 가속은 Dash 동안 차단하고 중력/외부 제어는 보존한다. 이동 종료 속도는 현재 허용 보행 속도 이하로 제한한다.

`UAbilityTask_AuthorizedMovement`는 서버 검증 예산 창만 제공한다.
탈것 정책에서 얻은 유한 프로필을 연결하고, 직업 SkillDefinition을 찾는 helper를 그대로 호출하지 않는다.
예측 이동·콜리전·액션 검증은 별도 Riding 실행기가 담당한다.
RMS handle, 승인 창 handle, Ability execution token을 한 실행 레코드에 보관한다.

서버 거절/하차/피격/사망/권한 폐기에는 해당 실행의 예측 RMS·FX·모션·임시 UI 쿨타임을 종료한다.
보정 후 서버 쿨타임으로 표시를 맞춘다. 승인된 뒤 취소된 Dash는 쿨타임을 유지한다.
입력에서 로컬만 예측됐다가 서버가 거절한 Dash는 서버 쿨타임을 소비하지 않는다.
예측 시작 전에 지면이 없거나 벽에 완전히 막혀 이동 가능한 길이가 0이면 활성화 자체를 거절한다.
승인 이후 충돌/피격은 환불 사유가 아니다.

### 5.4 공개 상태와 외형

Actor relevancy/Iris 수명을 따른다. Spawn/중도 접속/relevancy 재진입 때 현재 RepState만으로 Mounted 외형을 복원한다.
일회성 소리·파티클은 GameplayCue로 보낼 수 있지만 지속 탑승 상태를 Multicast에만 의존하지 않는다.
관전자/다른 플레이어에게 보유 목록·선택 설정·계정 정보를 복제하지 않는다.

기존 GetMesh의 CMC smoothing 관계는 유지한다. 탈것 mesh는 그 component transform을 따르는 표시 루트에 붙이고,
탑승자는 Riding AnimLayer에서 Saddle 기준의 골반/손/발 오프셋·IK를 적용한다.
기존 GetMesh를 탈것 소켓으로 재부착하거나 캐릭터/탈것 Actor를 서로 Attach하지 않는다.
탈것 mesh의 게임플레이 충돌·별도 물리 이동은 끈다. 이동 RootMotion은 RMS가 소유하고 표시용 애니메이션에는 root translation을 넣지 않는다.

소유자는 선택 탈것/대체 기본 외형을 요청 전에 비동기 preload한다. 준비 전 탑승 입력은 “외형 준비 중”으로 대기하며 취소할 수 있다.
원격 클라이언트는 로드 중 공용 placeholder를 표시하고 이동 상태는 계속 적용한다.
(현재 Pawn, Epoch, StateRevision, MountId, load request generation)이 모두 일치할 때만 로드 결과를 설치한다.
하차/교체/종료 시 자기 사운드·FX·카메라 보정·자세 핸들을 해제한다. 늦은 완료는 외형을 다시 생성하지 않는다.

## 6. 쿨타임·재접속 수명

**서버 PlayerState의 RidingSessionComponent가 단일 쿨타임 소유자**다.
게임플레이 검사는 단조 시간의 `DashReadyAt`를 사용한다. UI에는 서버 월드 시각과 매핑한 종료시각/남은 시간을 OwnerOnly로 전송한다.
월드 시간 배속/일시정지나 클라이언트 시계로 사용 가능 판정을 바꾸지 않는다.
GAS 표시 효과를 추가하더라도 권한 기준은 SessionComponent이며 별도 GE 타이머로 독립 소비하지 않는다.

| 사건 | 처리 |
|---|---|
| Dash 승인 | ReadyAt=서버 단조 현재시각+10초를 FX/콜백 전에 예약 |
| 하차/교체/Emote/피격 취소 | 기존 ReadyAt 유지 |
| 같은 PlayerState에서 Pawn 재생성 | 쿨타임/전투 잠금 유지, 새 Epoch의 OnFoot로 바인딩 |
| seamless travel로 PlayerState 복사 | 직접 비교 가능한 동일 서버 단조 시계라면 잔여시간만 복사. 새 월드에서는 표시 시각 다시 매핑 |
| 새 접속/새 게임 서버/복사 상태 유실 | **Dash에 10초 입장 보호 쿨타임**, 탑승과 일반 이동은 즉시 허용 |
| 전투 잠금 이력 없는 새 입장 | 탑승 전투 잠금은 서버 입장·현재 전투 정책에서 판단. Dash 보호와 별개 |
| 연결 일시 복원 | 기존 PlayerState를 재사용하면 기존 값 유지, 새 연결에는 입장 보호 적용 |

입장 보호는 v1의 명시적 편의성 절충이다. 오프라인 쿨타임 저장/이동기마다 HTTP 쓰기를 추가하지 않으면서 재접속 초기화 우회를 막는다.
v1 Dash 쿨타임의 상한도 10초로 검증한다. 따라서 어느 시점에 다시 들어와도 이전 실행의 남은 시간보다 짧게 준비되지 않는다.
나중에 10초보다 긴 이동기를 추가하면 이 상한/입장 보호/정책 버전을 함께 변경해야 한다.
전용 Emote는 새 접속 시 2초 보호를 적용한다. 캐릭터를 바꾸면 다른 캐릭터의 액션 상태를 공유하지 않는다.

## 7. 입력·퀵슬롯·보관함·HUD

### 7.1 키와 입력 소유

기본 키는 보관함 Alt+V, 탈것 호출 F8, Mounted에서 Dash=Space, Emote=Q, Dismount=R로 정의한다.
프로젝트 기존 키/사용자 리매핑과 충돌하면 기존 바인딩을 덮어쓰지 않고 새 동작은 미할당으로 표시해 키 설정에서 배정한다.
논리 태그는 `Input.Riding.OpenCollection/Toggle/Dash/Emote/Dismount`이며 실제 키는 입력 제공자에서 조회한다.

- 보관함/호출 입력은 플레이 가능 시 활성. Mounted 전용 입력 context는 현재 LocalPlayer/World에서만 활성화한다.
- 기본 이동/시점 키는 유지한다. 로아의 클릭 이동을 새로 구현하는 작업은 포함하지 않는다.
- 탑승 확정 시 기존 눌린 스킬 입력을 ReleaseAllSkillInputs로 종료한다.
- 하단 HUD 좌측 퀵슬롯 전환 담당의 공통 입력 라우터에 Riding 동작을 연결한다. 같은 키/클릭을 공용 슬롯과 Riding이 각각 처리하지 않으며, 키를 누른 뒤 표시 문맥이 바뀌면 옛 입력을 취소하고 새 동작을 자동 실행하지 않는다.
- 기본 전투 입력 dispatch는 Mounted에서 거절하고, UI 포커스/채팅을 먼저 존중한다. 두 context가 동시에 동일 키로 능력을 실행하지 않는다.
- 서버에서도 전투/소모 명령의 Mounted 조건을 검사한다. 클라이언트 IMC/HUD만 막는 것으로 끝내지 않는다.
- 하차·Pawn 교체·World 종료 시 자기 context/binding만 제거한다. 키 설정은 현재 값으로 다시 조회하며 탑승 전 스냅샷으로 전체 설정을 되돌리지 않는다.
- 기본값 복원/키 변경은 IRPGAdditionalInputProvider 및 기존 입력 트랜잭션 계약을 이용해 한 번만 저장한다.

### 7.2 v1 전용 퀵슬롯

기존 UQuickSlotComponent의 스킬/아이템 저장 형식을 탈것으로 재해석하지 않는다.
**RidingShortcut은 논리 슬롯 1개**이며, 화면 위치·셀·페이지 전환은 다른 담당의 하단 HUD 좌측 퀵슬롯 호스트에 연결한다. 기존의 별도 유틸리티 슬롯 추가안은 이 연결 방식으로 대체한다. 보관함에서 드래그하거나 선택 메뉴로 배정하며, 공용 슬롯의 최종 배치/드롭 인터페이스에 맞춘다.
타입은 `Selected | Specific(MountId) | RandomFavorites`, 설정은 캐릭터 RidingPreferences에 저장한다.
슬롯 클릭/F8은 같은 명령이다. 탑승 중 호출 키를 누르면 하차, 보관함의 다른 탈것 “탑승” 버튼은 명시적 교체다.

특정 탈것이 미보유/비활성이면 호출 불가 표시. Selected가 비어 있거나 즐겨찾기 후보가 없으면 자동으로 다른 탈것을 고르지 않는다.
무작위는 현재 구역에서 사용 가능한 즐겨찾기를 서버에서 균등 추첨한다.
같은 RequestSeq 재전송은 동일 결과를 돌려주고 재추첨하지 않는다.
여러 기존 아이템 퀵슬롯에 탈것을 넣는 범용 슬롯 개편은 v1에 필요하지 않다.

### 7.3 화면 상태와 저장

보관함: 보유/미보유, 검색, 즐겨찾기만 보기, 선택 표시, 현재 구역 사용 가능 여부, 탑승 버튼, 퀵슬롯 배정.
탑승 승인 상태를 하단 HUD 좌측 퀵슬롯 호스트에 제공하고, 호스트가 자신의 전환 규칙에 따라 Riding 콘텐츠를 표시한다. 기존 스킬 편성·서버 저장·직업 스킬 grant는 유지한다.
RidingActionBar는 이동기/모션/하차와 현재 키/쿨타임을 제공하는 논리 콘텐츠다. Riding 쪽에서 별도 공용 스킬바/전환기를 만들거나 부모 HUD를 직접 교체하지 않는다. 구체적인 페이지/셀 위젯 소유는 HUD 담당 계약에 맞추고, 표시 전환 자체로 탑승/하차·서버 상태가 바뀌지는 않는다.
사용자의 2026-11-06 담당 분리에 따른 [제공/소비·입력·수명 인계 계약](RIDING_SYSTEM_ARCHITECTURE.md#하단-hud-좌측-퀵슬롯-전환-연계--2026-11-06)을 따른다. 현재 신규 공용 인터페이스의 구현/합의가 완료됐다는 뜻은 아니다.

UI 상태는 Loading / Ready / Saving / ResultUnknown / Error다.
영속 등록·선택·즐겨찾기·슬롯 편집은 로비에서만 허용한다. 던전에서는 보관함 조회와 이미 보유한 탈것 탑승/교체만 가능하다.
활성 세션에서 새 탈것을 보유하게 된 경우 다음 입장 스냅샷부터 사용한다. v1 보유 해금은 추가만 가능하므로 기존 스냅샷의 보유가 세션 도중 취소되는 API는 만들지 않는다.
새 정책으로 비활성화된 정의는 서버의 콘텐츠/사용 제한 정책에서 차단한다.

저장 타임아웃은 성공/실패로 추정하지 않는다. UIManager의 작은 로컬 명령 저널에 인증 토큰 없이
CharacterId, RequestId, 명령 종류/본문, 예상 리비전만 남기고 같은 명령의 영수증을 조회/재시도한다.
계정/캐릭터가 바뀌면 이전 화면에 결과를 적용하지 않는다. 원래 소유자가 다시 로그인한 뒤 해당 요청을 확인한다.
보유·선택은 서버 응답이 확정될 때 적용한다. 재시도 중 새 ID로 등록을 다시 보내거나 클라이언트에서 아이템을 빼지 않는다.

## 8. 백엔드 API·저장 계약

기존 [Roster/GameCharacter 모델](../Backend/ProjectRpg.Backend/Domain/DomainModels.cs)을 사용한다.
보유 범위는 **RosterId**다. WorldId는 지속 세계이고, 임시 Dedicated Server의 ServerId/DungeonSessionId를 보유 키로 쓰지 않는다.
선택·즐겨찾기·RidingShortcut은 CharacterId별이다. 계정 전체의 다른 World 원정대로 해금을 확장하지 않는다.

### 8.1 엔드포인트

모든 경로는 신규 설계다. 실제 공개 전 인증·본문 크기·요청 예산과 응답 계약을 구현한다.

| 메서드/경로 | 권한 | 입력/결과 |
|---|---|---|
| GET /api/characters/{characterId}/riding | 소유 Player | CatalogVersion, 보유 ID/definitionVersion, rosterRevision, preferences, preferencesRevision, canEdit |
| POST /api/characters/{characterId}/riding/register | 소유 Player, 로비 쓰기 조건 | RequestId, ItemId, ExpectedItemRevision, CatalogVersion → 등록 결과·현재 아이템·보유 리비전 |
| PUT /api/characters/{characterId}/riding/preferences | 소유 Player, 로비 쓰기 조건 | RequestId, ExpectedRevision, SelectedMountId(nullable), FavoriteMountIds, Shortcut → 설정 전체 교체 |
| GET /api/characters/{characterId}/riding/requests/{requestId} | 동일 소유 Player | 최초 확정 결과 또는 NotFound. 다른 캐릭터 요청은 조회 불가 |
| GET /api/game-servers/sessions/{sessionId}/members/{characterId}/riding | 해당 할당/확정 멤버십 GameServer | 입장용 보유/설정 스냅샷과 CatalogVersion/Hash, CharacterId/RosterId/ConnectionGeneration |

Player는 SteamId/RosterId/WorldId/소모량/속도/해금할 MountId를 등록 요청에 지정하지 않는다.
MountId는 서버의 ItemDefinition 매핑에서 얻는다. 일반 아이템 Commit의 권한을 Player에게 개방하지 않는다.
GameServer snapshot은 session/server/character/connection generation을 검증하고 telemetry-only 자격을 거절한다.
HTTP 오류/인증 만료를 영구 멤버십 폐기로 해석하지 않는다. 확정 입장이 없으면 Riding이 준비되지 않은 상태로 남는다.

쓰기 본문 최대 16KiB, 고정 ID 길이 64, 즐겨찾기 32개, 등록 수량 1.
일반 조회는 정의 최대 512개에서 응답을 제한하고 아이콘/에셋 바이너리는 포함하지 않는다.
Riding API는 계정별 burst 10, 초당 2회 쓰기·초당 5회 조회 예산을 둔다.
int64 리비전은 JSON에서 10진 문자열로 전달한다. 0은 아직 저장된 설정이 없을 때만 사용하며 overflow 전에는 쓰기를 거절한다.
알 수 없는 필드/명령 종류, 중복 즐겨찾기, 미보유 참조, 잘못된 ID·리비전은 거절한다.

등록 예시:

```json
{
  "requestId": "ee325afb-63ce-4d09-8694-070dff2fd932",
  "itemId": "5aa9b4bb-bf41-4dcb-95cc-9f4e4452c6dd",
  "expectedItemRevision": "17",
  "catalogVersion": 1
}
```

설정 예시:

```json
{
  "requestId": "87d6dad0-2ddb-470d-a8c6-a41e23e7a194",
  "expectedRevision": "2",
  "selectedMountId": "mount.horse.brown",
  "favoriteMountIds": ["mount.horse.brown"],
  "shortcut": {
    "mode": "Selected",
    "mountId": null
  }
}
```

Selected/RandomFavorites 슬롯의 MountId는 null이어야 한다. Specific은 보유한 유효 ID를 요구한다.
selectedMountId=null과 빈 favorites는 유효하며 “선택 없음/후보 없음”으로 표시한다.

### 8.2 결과·재시도

| HTTP / 결과 | 의미 |
|---|---|
| 200 Registered / PreferencesSaved | 신규 확정 결과 |
| 200 AlreadyApplied | 같은 RequestId/정규화 명령 지문의 최초 결과 재생. 현재 상태는 별도 조회로 갱신 |
| 200 AlreadyOwned | 새 요청이지만 이미 보유. 아이템 소비0, 보유 revision 증가0. 이 결과도 영수증 보존 |
| 400 InvalidRequest / DefinitionMismatch | 형식/정의 버전 오류. 본문 수정 시 새 RequestId |
| 401 / 403 / 404 | 인증 실패 / 권한 부족 / 소유 범위 내 대상 없음. 타인 존재를 노출하지 않음 |
| 409 RevisionConflict / IdempotencyConflict | stale 리비전 또는 같은 요청 ID의 다른 내용. 자동으로 새 리비전으로 덮어쓰지 않음 |
| 409 ActiveSession / SettlementPending / AssetReserved / ItemLocked | 현재 쓰기 불가. 확정 자산 변화 없음 |
| 409 Busy | NOWAIT 경합, 같은 요청으로 제한 재시도 |
| 429 / 5xx / timeout | 속도 제한 또는 결과 미확정. Retry-After/영수증 조회와 같은 요청 재시도 |

클라이언트 재시도 간격은 1/2/4초, 이후 ResultUnknown에서 수동 재시도·다음 로그인 복구다.
백엔드 내부 직렬화 실패/교착은 같은 명령으로 최대 3회 jitter 재시도하고, 응답 불명은 원자 영수증으로 판별한다.
GET receipt의 NotFound만 보고 이미 진행 중인 요청이 실패했다고 단정하지 않는다. 같은 RequestId 재전송으로 수렴한다.

### 8.3 테이블과 제약

| 테이블(신규 후보명) | 키/필드·제약 |
|---|---|
| riding_roster_state | roster_id PK/FK, revision bigint>=0 |
| riding_mount_unlocks | (roster_id, mount_id) PK, definition_version, unlocked_at_utc, source_request_id. 동일 원정대 탈것 중복 불가 |
| riding_character_preferences | character_id PK/FK, selected_mount_id nullable, shortcut_mode, shortcut_mount_id nullable, revision bigint>=1 |
| riding_character_favorites | (character_id, mount_id) PK. 단일 설정 트랜잭션에서 최대32·보유·정의 검사 |
| riding_command_receipts | request_id UUID PK, operation, character_id, roster_id, canonical_fingerprint, outcome, result_payload, committed_at_utc, child_item_request_id nullable |

명령 지문은 프로토콜/operation/인증된 character·roster/요청의 의미 필드 전체를 정규화해 만든다.
즐겨찾기는 정렬 후 지문화하여 표시 순서 차이로 다른 명령이 되지 않게 한다.
같은 Riding RequestId를 다른 operation/character/본문에 재사용하면 충돌이다. 다른 기능의 요청 ID 공간과 혼동하지 않는다.
등록의 기존 Item V2 하위 영수증 ID는 RequestId와 고정 namespace `Riding.Register/v1`에서 결정적으로 파생하고 부모 영수증에 연결한다.
부모/자식 영수증과 소비/해금은 동일 connection/transaction에서 확정한다. 하위 Commit API를 먼저 따로 호출하지 않는다.

FK는 기존 roster/character를 참조한다. 교차 테이블 보유 조건/최대32는 공통 잠금 안에서 검사한다.
삭제는 기존 계정/캐릭터 삭제 계약에 맞춰 연결하되 개별 캐릭터 삭제가 원정대 공유 해금을 지우지 않는다.
영수증은 v1에서 TTL로 지우지 않는다. migration 번호는 구현 시 다음 미사용 번호를 배정한다.

### 8.4 원자 등록과 거래/입장 경합

등록은 장비 제작/분해처럼 인증된 로비 명령으로 구현하며 기존 아이템 공통 저장 검사를 통과한다.

1. 인증/캐릭터 소유 확인 후 같은 RequestId의 기존 Riding 영수증을 찾는다. 지문이 같으면 현재 아이템 위치/등록 여부보다 먼저 최초 결과를 재생한다.
2. 기존 account-access gate → character FOR UPDATE → lease FOR UPDATE NOWAIT 순서를 사용한다. 현재 계정 제재·활성 입장/리스·진행/미확정 정산을 검사한다. 빈 lease와 새 입장의 FK 경합도 character lock으로 직렬화한다.
3. Riding request advisory → roster Riding-state 전용 행 잠금 → 기존 아이템 owner advisory → 아이템 행 순으로 잠근다. 그 뒤 영수증·현재 소유·보유를 재검사한다.
4. 전용 roster 상태 행은 최초 요청에서 충돌 안전하게 생성한다. 기본 roster 행 전체를 UPDATE lock하지 않아 불필요한 FK 역순 잠금을 만들지 않는다.
5. 아이템은 해당 캐릭터 Inventory의 Active/유효기한 내/사용자 잠금 없음/리비전 일치/활성 거래 예약 없음이어야 한다. 등록 아이템 매핑/버전과 수량>=1을 검사한다.
6. 이미 보유하면 AlreadyOwned 영수증만 기록하고 아이템·보유 리비전을 바꾸지 않는다. 다른 부캐의 동시 등록도 같은 roster 상태 잠금/PK로 단일 해금에 수렴한다.
7. 신규이면 아이템 1개 소비(잔량0은 기존 tombstone 계약), 해금 insert, roster revision+1, 부모/자식 영수증을 함께 commit한다. 중간 실패는 전부 rollback한다.
8. 저장 확정 후 응답·인벤토리 projection을 갱신한다. UE 콜백/GAS 효과 실행을 영속 해금의 근거로 사용하지 않는다.

아이템 하위 저장은 기존 transaction 안의 명령 포트를 호출하며 이미 보유한 잠금을 재사용한다.
거래 예약은 character→owner→item 순서를 공유하고 예약/소비 검사를 공통 Commit에서 제공한다.
Riding은 잠금을 취득한 뒤 현재 DB 시각으로 거래 예약/유효기한을 검사하고 만료 작업 완료를 기다리지 않는다.
Riding 전용 잠금을 다른 일반 아이템 경로가 역으로 요청하지 않도록 의존을 단방향으로 유지한다.
동일 원정대 부캐·입장·거래 예약과의 실PG 경합 검사가 완료 조건이다.

Preferences 변경은 같은 account/character/lease 경계와 Riding request→roster-state→preferences 순서를 쓰고 설정 revision CAS를 검사한다.
로비 쓰기 거절은 기존 성공 영수증 재생을 실패로 바꾸지 않는다. 기존 영수증 조회도 현재 인증/소유 범위를 통과해야 한다.
Memory 구현은 기존 InMemoryTransactionGate를 공유한다. 기능별 임시 dictionary 잠금으로 아이템/해금을 따로 확정하지 않는다.

### 8.5 게임 서버 보유 스냅샷

확정 입장 후 서버가 전용 GET으로 스냅샷을 취득한다. 캐릭터·원정대·할당 세션·ConnectionGeneration 및 CatalogVersion/Hash를 고정한다.
HTTP 완료 시 동일 PlayerState/신원/세대인지 재검사한다. 오래된 응답은 새 Pawn/계정에 적용하지 않는다.
초기 실패는 Riding만 NotReady이며 기존 캐릭터 전투/보상 권한을 철회하지 않는다.
서버는 1/2/4초 후 재시도하고 이후 사용자의 새 탑승 요청에서 최대 10초에 한 번만 재조회한다.
전체 게임 서버가 주기적으로 백엔드를 호출하거나 이동 프레임마다 보유를 조회하지 않는다.

## 9. 구현 파일·에셋 연결과 순서

### 9.1 수정 범위 지도

| 분야 | 새 경로 / 기존 연결점 | 필요한 변경 |
|---|---|---|
| 정책·상태 | Public/Private/Riding/RPGRidingTypes, Policy, Component, SessionComponent | 정의 검증·상태 전이·요청 예산·OwnerOnly 세션 데이터 |
| 탈것 정의 | Public/Private/Riding/RPGMountDefinition, RPGMountUnlockFragment | DataAsset와 등록 아이템 전용 Fragment. 일반 소모품 효과와 혼합 거절 |
| 이동 | Component/RPGCharacterMovementComponent, RPGPlayerMovementComponent와 Riding prediction 어댑터 | 프로필 계산·SavedMove/move response·경계 sweep·RMS/승인 창 |
| 전투 | Component/RPGAbilitySystemComponent, CombatControl, 승인 피해/제어 적용 경계 | Mounted 중 플레이어 전투/소모 차단, 유효 타격/피격 후 전투 잠금/하차 알림 |
| Pawn/신원 | Character/RPGPlayer, Player/RPGPlayerState | 컴포넌트 생성, ASC ready/인증 세대/새 Pawn·권한 폐기 구독 |
| 액션 | Public/Private/Riding/Abilities | Dash/Emote AbilitySet, 취소·예측·자기 핸들 정리 |
| 표시 | Riding/RPGRidingPresentationComponent 및 Anim/Riding layer | 공통 placeholder, Saddle/IK, 비동기 세대, FX·카메라 수명 |
| UI/입력 | UI/Riding, Input의 추가 provider, 공용 좌측 퀵슬롯 호스트 어댑터 | Riding 상태/액션/슬롯 데이터·보관함 담당. 공용 HUD 배치/전환/입력 dispatch는 별도 HUD 담당과 인계 후 연결 |
| 백엔드 | Riding/**, Api/RidingApiEndpoints, Data의 Memory/PG 어댑터, 새 migration | 정의 manifest, typed port, 원자 등록·설정·영수증·입장용 snapshot |
| 유지 검사 | Private/Tests/RPGRiding*Tests, Backend/ProjectRpg.Backend.RidingTests, Scripts/Tests/Run-RidingTests.ps1 | 단위/native, HTTP/PG 및 별도 네트워크 실행 |
| 에셋/저작 | Content/Blueprints/Riding/**, 기존 HUD에 Riding 모듈 인스턴스 | 두 탈것, 몽타주/소켓·등록 아이템·아이콘, cook 참조, JSON export/검사 |

표의 클래스/파일은 새 설계 후보이며 실제 구현 시 기존 이름/모듈 export와 맞춘다.
인벤토리 플러그인에 Project_RPG/GAS/HUD 의존을 넣지 않는다.
ASC 활성화 가드는 입력 출처가 아니라 서버가 가진 Ability 분류(수동 전투/아이템/시스템/패시브)로 판단한다.
Riding 태그를 이유로 시스템 초기화·기존 지속 버프·서버 피해 처리를 일괄 차단하지 않는다.
직접 RPC/퀵슬롯/일반 GAS event/이미 진행 중 Ability 경로도 같은 자격 규칙을 사용한다.

### 9.2 에셋 준비와 공개 조건

- 첫 모델은 말, 두 번째는 동일 성능의 다른 탈것으로 작성한다. 임시 모델은 개발 표시로 구분한다.
- 각 모델의 Saddle socket, RiderPoseLayer, 정지/이동/돌진/전용 모션, 아이콘과 등록 아이템을 연결한다.
- v1의 고정 캡슐 바깥 장식은 이동 충돌로 취급하지 않는다. 문/벽에서 시각 관통이 과하면 모델 크기·오프셋을 수정한다.
- 카메라는 기본 pivot을 유지하고 전용 보정은 additive 핸들로 적용/회수한다. 다른 카메라 모드의 값을 저장/복구하지 않는다.
- soft 참조는 PrimaryAsset cook 규칙/Experience 참조에 포함한다. 에디터에서만 로드되는 성공을 패키지 성공으로 보고하지 않는다.
- 전용 서버는 정의/정책을 사용하되 시각 모델의 동기 로드·애니메이션 Notify로 판정하지 않는다.
- 기본 Feature flag는 구현/통합 검증 전 비활성. 클라이언트와 서버의 동일 ProtocolVersion/CatalogVersion을 확인한 배포에서 활성화한다.

### 9.3 단계별 완료 기준

| 단계 | 변경 단위 | 종료 조건 |
|---|---|---|
| M0 — 상세 설계 | 본 문서의 규칙/상태/소유/API/DB/검증 계약 | 이번 문서 작업으로 완료. 게임 기능 완료와 구분 |
| M1 — 상태·일반 이동 | 정의/정책, Session/Pawn 컴포넌트, 탑승/하차/복제, 이동 프로필/기본 외형 | R01~R03, R06 통과. 개발 fixture는 제품 보유 권한 경로와 분리 |
| M2 — 이동기·입력/HUD | 예측 Dash/공통 쿨타임, Emote, 서버 전투 차단, Riding 콘텐츠/입력 어댑터와 별도 담당의 공용 좌측 퀵슬롯 연결 | R04~R09 통과. 재탑승·교체·키 변경에 기존 편성 유지. Riding 제공자만 검사한 결과와 실제 공용 호스트 통합 결과를 구분 |
| M3 — 영속 등록·보관함 | 실제 인증·PG/Memory·등록/설정/영수증·UI/입장 snapshot | R10~R14 통과. 실제 획득→등록→탑승→재로그인 |
| M4 — 두 번째 탈것·저작/구역 | exporter, 두 에셋, 즐겨찾기/무작위·금지 볼륨·콘텐츠 배치 | R06/R08/R15 통과. 실행 코드 수정 없이 두 번째 외형 연결 |
| M5 — 전체 통합·배포 검증 | 실제 로비/던전/보스 경계/사망/재접속, 대상 패키지 | R01~R16 중 해당 검사 전부와 실제 입력/GPU 확인. 결과/미검증 범위 기록 |

M1 완료를 전체 탈것 기능 완료로 표현하지 않는다. 구조를 다시 결정해야 하는 예정 M0는 남겨두지 않는다.
플레이 테스트는 고정된 계약 아래 초기 수치/외형을 조정하는 단계이며 정책 변경이 필요하면 해당 결정과 검증 사례를 함께 갱신한다.

## 10. 검증 행렬

문서 작성에서는 링크/형식/계약 대조만 수행한다. 아래 항목은 **향후 구현의 통과 기준**이다.

| ID | 사례 | 기대 결과 / 증거 |
|---|---|---|
| R01 | 정상 탑승/하차/같은 ID/교체·연타/중복 Seq | 단일 상태 전이, 멱등 결과, 거절 시 기존 상태 보존 |
| R02 | 서버+클라이언트2, 80~160ms/5% 유실·move와 RepNotify 역순 | 서버/본인/상대 상태 일치, 보정 후 오래된 속도/액션 복원0 |
| R03 | 보행 중 전환/기존 이속 버프/상한300/스턴/도보 복귀 | Mounted600·상한300·스턴0, 도보 원래 GAS 계산. 서버 보안 오탐0 |
| R04 | Dash 예측 성공/서버 거절/피격/하차/종료 경합 | 승인은1회 쿨타임, 거절은0회, 승인 후 취소 환불0, 자기 RMS/예산 잔존0 |
| R05 | Dash→탈것 교체→하차/재탑승, Pawn 교체, 새 접속 | 기존 남은 쿨타임 유지, 새 접속10초 보호, 속도 우회0 |
| R06 | 벽/천장/문/계단/발판/경사/낭떠러지·고속 구역 통과 | capsule sweep/바닥/구역 규칙 일치, 관통/임의 순간이동0 |
| R07 | 일반/보호막 피해·DoT·잔존 공격·완전 면역·감속/CC | 2절 정책과 동일한 하차/10초 잠금. 면역·피해·제어 원래 계약 보존 |
| R08 | 중도 접속/relevancy 재진입/외형 로드 지연/실패/교체 중 완료 | 현재 상태만 표시, 오래된 모델/모션/소리 설치0, placeholder에서 복구 |
| R09 | Space/Q/R remap·기본값 복원·옵션/채팅·누른 키 상태 탑승 | 전투와 전용 액션 중복 실행0, 종료 후 편성/포커스/현재 키 보존 |
| R10 | 실HTTP·실PG 등록/서버 재시작/응답 유실/동일 ID 다른 본문 | 소비·해금·영수증 원자성, 최초 결과 재생, 충돌 거절 |
| R11 | 동일 원정대 부캐2의 같은 탈것 동시 등록/다른 원정대 | 해금1/소비1, 다른 등록 아이템 보존, 다른 원정대 공유0 |
| R12 | 거래 예약/입장 FK·lease/heartbeat/정산/계정 제재와 등록 경합 | 권한/예약/로비 검사 우회0, 교착 제한 재시도·전체 rollback 확인 |
| R13 | Preferences CAS/미보유/즐겨찾기33개/빈 선택/슬롯모드 | lost update0, 규격 위반 거절, 빈 상태 정상, 재접속 복원 |
| R14 | 오래된 snapshot/잘못된 DS·telemetry 토큰/다른 계정·연결 세대 | 새 신원에 적용0, 정보 노출0, 초기 실패가 전투/보상 폐기0 |
| R15 | 두 탈것/서버·클라 catalog 불일치/빠진 cook/소켓/루트모션 | 데이터 검사 거절 또는 명시적 미준비 표시. BP마다 별도 이동 로직0 |
| R16 | 로비 등록·설정→입장→통로 탑승→보스 구역 하차→정산/복귀 | 기존 2~4인 입장·전투·보상·인벤토리/우편 흐름 회귀0 |

단위/native, 실제 HTTP/Memory/PG, Iris 네트워크, GPU/실제 키 입력, 패키징을 각각 구분한다.
통과 숫자만 남기지 말고 입력 commit/모듈 hash·실행 조건·결과와 알려진 제한을 기록한다.
검증 전에 자기 임시 경로를 예약하고 종료 finally에서 사본·빌드 객체·DLL/PDB·캐시·중복 로그를 폐기한다.
원본 Binaries/Intermediate·공유 DDC·사용자 에디터·다른 담당 실행물은 정리 대상이 아니다.

## 11. 설계 검토 결과와 인계

이번 상세화로 초안의 미정 항목을 다음과 같이 닫았다.

| 초안의 미정 항목 | 이번 결정 |
|---|---|
| 전투·피격·상호작용 | 2절의 10초 잠금/유효 피격 하차/상호작용 하차 후 재검사 |
| 지역·하차 위치 | 기본 Deny+명시적 Allow/금지 우선, 동일 캡슐 위치 하차 |
| 속도/이동기/충돌 | 600/900cm·0.6초/10초, 지상 돌진, 고정42/96 캡슐 |
| 상태·예측 | Epoch/리비전/액션 순서, 서버현재상태 우선·SavedMove/move response 확장 |
| 쿨타임 소유/재접속 | PlayerState 단일 권위, Pawn 교체 유지·새 접속10초 보호 |
| 보유 공유·설정 | 기존 RosterId 공유, CharacterId별 설정/즐겨찾기·전용 슬롯1개 |
| 등록·영수증 | 로비 전용 typed API, 아이템1소비+해금+부모/자식 영수증 단일 트랜잭션 |
| 입력/HUD | 기존 스킬 편성 보존, 추가 provider·공용 좌측 퀵슬롯에 Riding 콘텐츠 제공·서버 사용 가드 |
| 네이밍 | 기능/동작=Riding, 개별 탈것 정의=Mount |

구현 착수 시 [AGENTS.md](../AGENTS.md)와 [로드맵 12절/작업 조율](Online_RPG_Development_Roadmap.md)을 재조회한다.
이번 예약은 문서만이며 제품 Source/Backend/에셋을 선점하지 않는다.
인벤토리 플러그인·거래 예약·공통 Controller/PlayerState/ASC/이동 컴포넌트의 실제 게시 상태를 확인해 순차 예약한다.
최신 호출과 계약이 바뀌었으면 아래 불변식을 유지하며 어댑터 위치만 맞추고, 기능을 중복 구현하지 않는다.

최종 불변식:
1. 서버 보유·구역·행동 검사 없이 Mounted/액션을 확정하지 않는다.
2. 탈것 교체/하차/재접속으로 이동기 대기시간을 단축하지 않는다.
3. 기존 전투·제어·입력·HUD의 다른 소유 상태를 Riding 정리로 제거하지 않는다.
4. 등록 아이템 소비와 원정대 해금은 함께 성공하거나 함께 실패한다.
5. 기존 아이템 리비전·거래 예약·입장/정산 잠금·권한 폐기 경계를 우회하지 않는다.
6. 중도 접속과 늦은 콜백에서도 현재 Epoch/리비전의 외형·입력·이동만 남는다.

이번 변경은 **상세 설계 문서 완료**이며 게임 기능 구현·검증 성공을 뜻하지 않는다.
문서/단계 완료는 자기 변경만 커밋·푸시·원격 해시 확인 후 보고한다. 구현 검증만 수행한 작업의 게시 예외는 AGENTS.md를 따른다.
