# 월드 아이템 드롭 연출

`ARPGPickUpBase`의 기존 서버 획득·백엔드 영수증 흐름 위에
`URPGLootPresentationComponent`가 외형만 담당한다. NPC 사망 시 드롭 테이블의
확률·종류·수량은 유지하고, 당첨한 픽업을 주변 지면으로 펼친다.

팩의 `BP_boots` 등은 일반 Actor 시연용 콘텐츠다. 이 액터를 드롭 테이블의
획득 대상으로 바꾸지 않고, 기존 `ARPGPickUpBase` 자식에서 Niagara만 재사용한다.

## 드롭에서 획득까지

- NPC 아래 지면을 찾고 65~180cm 반경에 순서별로 다른 착지 후보를 배치한다.
  경사가 심하거나 지면이 없거나 벽으로 막힌 후보는 원래 지면으로 되돌린다.
- 서버의 실제 픽업과 충돌체는 착지 위치에 생성한다. 화면 메시만 사망 위치에서
  약 0.7초 동안 포물선으로 날아가고, 작은 착지 반동 뒤 정지한다.
  여러 항목은 최대 0.3초 범위에서 순서대로 출발한다.
- 비행 중에는 서버 획득 검사가 거절한다. 판정은 서버 시간으로 계산하므로
  전용 서버에서 연출 Tick이나 Niagara 로딩이 필요하지 않다.
- 착지 시 `Content/DrapEffet/VFX`의 Niagara와 공간 사운드를 재생한다.
  빛기둥은 아이템 메시의 배율·회전과 독립적으로 똑바로 선다.
- 예약 중에는 빛기둥을 숨기고 거절 시 복원한다. 획득 완료 연출은 기존 지급
  컴포넌트가 영수증을 확정한 뒤 `PickedUp()`에서만 호출한다.
  풀 반환·재사용·액터 종료 때 프록시/이펙트/비동기 로드를 정리한다.
- 원래 배치된 픽업은 날아오거나 드롭 소리를 내지 않고 바닥 이펙트만 표시한다.
  이미 착지가 끝난 뒤 관련성이 생기거나 로딩이 끝나도 비행·착지 소리를 재생하지 않는다.

## 등급과 작성 설정

픽업 Blueprint의 `LootPresentation` 컴포넌트에서 `Rarity`, `GroundEffect`,
`EffectScale`, `FlightDuration`, `ArcHeight`, `LandingSound`, `CollectedSound`,
`SoundVolume`을 설정한다. `bEnabled=false`는 기존 즉시 획득/기존 메시 표현을 유지한다.
등급은 **월드 연출의 작성 값**이며 아이템 능력치·드롭 확률·영속 등급을 만들지 않는다.
현행 Definition에는 확정된 공통 등급 계약이 없으므로 기존 아이템을 임의로
희귀/전설로 승격하지 않고 기본 Common을 사용한다.

| 표현 등급 | 기본 Niagara | 기본 착지 Cue |
|---|---|---|
| Common | NE_drop_effect05 | Pop_02_Cue |
| Uncommon | NE_drop_effects06 | Abstract_Pop_03_Cue |
| Rare | NE_drop_effects03 | Special_Musical_01_Cue |
| Epic | NE_drop_effects04 | Special_Powerup_04_Cue |
| Legendary | NE_drop_effects01 | Special_Powerup_10_Cue |

사운드는 기존 `Interface_And_Item_Sounds/Cues`를 사용한다. 공통
`SC_LootDrops`는 최대 4개 음성과 짧은 재트리거 간격으로 다량 드롭의 소리 중첩을 제한한다.
기본 획득 Cue는 `Abstract_Pop_01_Cue`다. `DA_LootPresentationAssets`의 명시적
AlwaysCook 라벨은 코드에서 선택하는 VFX 7개·Cue 6개·공통 concurrency와 그 의존성을 보존한다.

`Scripts/Editor/ConfigureLootPresentation.py`는 기본 읽기 전용 검사이며
`-RPGApplyLootPresentation`에서 누락된 두 Integration 에셋만 만든다.
기존 작성값이 다르면 덮어쓰지 않고 중단한다. 원본 VFX·메시·드롭 테이블·맵을 저장하지 않는다.

## 검증 경계

집중 검사는 `Scripts/Tests/Run-LootPresentationTests.ps1`에서 실행한다.
실제 컴포넌트의 궤적·서버 권한/시간/재사용·작성 에셋·시각 컴포넌트 수명과
원래 충돌체 보존을 검사하고, 게시 Source/Config/플러그인 사본에 변경분을 합쳐
전체 UHT 및 영향 CPP를 컴파일한다. 사본과 임시 DLL/OBJ/UserDir/cache는 종료 때 정리한다.

실제 NPC 자연 전투·Steam 다인·운영 HTTP/DB 정산의 재검증과 Shipping 패키징을
이 집중 검사의 성공으로 간주하지 않는다. 던전 클리어 보상의 가방 우선/초과 우편
지급은 그대로이며, 이번 월드 연출이 클리어 보상을 바닥 픽업으로 바꾸지는 않는다.

### 2026-11-16 확인 결과

- 독립 모듈의 실제 컴포넌트 native 4개가 오류/경고 없이 통과했다.
  게시 프로젝트 사본의 전체 UHT 및 영향 CPP 4개도 컴파일했다.
- 정상 Editor 타깃 36단계 빌드·링크가 성공했다. 당시 설치 DLL
  `93022E19…87642A8`에서 본인 4개와 기존 대검 2개/NPC 드롭 4개가 성공했다.
  기존 무효 드롭 행을 거절하는 경고 6개는 원래 검사 목적에 맞춰 원로그로 대조했다.
- 이후 다른 담당자의 Development 소스 2개가 달라진 사실은 당시 DLL 증거와 분리했다.
  이 결과를 후속 변경을 포함한 현재 전체 트리의 빌드 성공으로 확대하지 않는다.
  본인 구현/회귀 입력 6개는 독립 검사와 설치 DLL 검사 당시 값이 유지됐다.
- 독립 D3D12 장면에서 실제 컴포넌트의 비행/착지와 원본 5종을 렌더했다.
  비교 장면에는 플레이어가 없어 단독 시뮬레이션을 사용하고 실제 게임 뷰 렌더 경로를
  준비했다. 단순 SceneCapture에서 입자가 보이지 않던 초기 캡처는 시각 성공으로 취급하지 않았다.
- `ConfigureLootPresentation.py`를 새 프로세스에서 읽기 전용으로 재실행해
  저장된 cook 라벨의 명시 참조와 동시 음성 최대 4개를 재확인했다. 재저장 0개다.

작은 로컬 결과·명령·해시와 최종 캡처는
`Saved/BuildChecks/LootPresentation-20261115/` 및 집중 실행기의 결과 폴더에 보존한다.
사설 DLL/OBJ/소스 사본/UserDir/cache는 작업 종료 시 폐기하며 원본 게임 출력은 유지한다.
