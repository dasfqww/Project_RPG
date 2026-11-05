# 인벤토리 플러그인 경계

인벤토리 공통 런타임은 `Plugins/RPGInventory`의 `RPGInventoryRuntime` 모듈에 둔다.
프로젝트는 이 모듈에 의존하며, 플러그인은 `Project_RPG` 모듈에 의존하지 않는다.
일반 Runtime 플러그인으로 활성화하며 Experience 전환 때 아이템 상태를 해제하지 않는다.

## 소유 범위

| 위치 | 책임 |
|---|---|
| 플러그인 `Public/Item/Definition` | 기본 아이템 정의·정의 Fragment 계약·정의 조회 카탈로그 |
| 플러그인 `Item/RPGItemRuntimeTypes`, `RPGItemInstance`, `RPGItemFactory` | 인스턴스 상태·생성·고정 ID와 수량 |
| 플러그인 `Item/Persistence`, `Item/Policy`, `Item/Transaction` | 아이템 Record, 저장소 계약, 가방/스택 규칙, 이동·교환·병합·분할·장착·소비 명령 계획/실행, 미확정 요청 저널 |
| 플러그인 `Item/Projection` | 소유자에게 공개할 데이터 변환·저장·FastArray 복제 데이터 |
| 플러그인 `Item/Backend` | 기존 Item V2 통신·JSON·영수증 확인·재시도와 서버 저장 연동 |
| 게임 모듈 `RPGItemCommandComponent`, `RPGInventoryProjectionComponent` | 인증된 PlayerState와 서버 RPC·보안·장비·GAS·보상 흐름 연결 |
| 게임 모듈 `RPGItemDefinitionFragments`, `RPGItemActionPolicyRegistry` | 게임별 장비/소비 효과 정의를 공통 명령 정책으로 변환 |
| 게임 모듈의 인벤토리 UI·ViewModel | 기존 창·슬롯·입력·퀵슬롯·장비 화면과 표시 연결 |

`Item/Weapon`, 월드 픽업 액터, 구형 Blueprint 호환 facade는 게임 연결부다.
`URPGInventoryComponent`는 이미 퇴역한 호환 클래스이며 새 인벤토리 저장소로 사용하지 않는다.
플러그인 추출을 위해 캐릭터·HUD·컨트롤러·전투 코드를 플러그인으로 함께 끌어들이지 않는다.

## 의존성과 알림

- 기존 `#include "Item/..."` 경로를 유지하고 `Project_RPG.Build.cs`의 public 의존성으로 노출한다.
- 플러그인의 내보내기 매크로는 `RPGINVENTORYRUNTIME_API`다.
- `EItemCategory`와 `EEquipmentSlotType`은 플러그인의 `Item/RPGInventoryTypes.h`가 소유한다.
  기존 `Type/RPGEnumTypes.h`가 이를 포함하므로 기존 게임 코드의 include도 유지된다.
- 명령 실행기는 `IRPGItemActionPolicyCatalog` 등 공통 계약을 소비한다.
  게임별 장비/소비 Fragment를 검사하는 Registry는 게임 모듈에 둔다.
- FastArray는 구체적인 게임 컴포넌트 대신 `FSimpleDelegate`로 수신 완료를 알린다.
  게임 컴포넌트는 `CreateUObject`로 약한 소유자 바인딩을 제공한다. 콜백은 복제/저장되지 않는다.
- 기존 owner-only 복제, 서버 신원 검사, 저장 확정 후 적용, 중복 지급 방지와 복구 계약을 유지한다.
  ASP.NET Core/PostgreSQL 서비스는 UE 플러그인 외부의 기존 저장 권위다.

## 기존 콘텐츠와 설정 호환

- 이동한 Class/Struct/Enum 23개에 `Config/DefaultEngine.ini`의 개별 CoreRedirect를 제공한다.
  관련 없는 게임 타입 전체를 리다이렉트하는 PackageRedirect는 사용하지 않는다.
- 아이템 에셋은 기존 `/Game/Blueprints/GameData/Item` 경로에 둔다.
  PrimaryAssetType/태그 기반 ID, 저장/HTTP 데이터 형식을 변경하지 않는다.
  `UDataManager`는 CoreRedirect의 이전 클래스 경로도 검색하므로 에셋을 다시 저장하지 않아도
  기존 에셋의 레지스트리 메타데이터에서 네이티브 정의를 찾을 수 있다.
- `DefaultGame.ini`의 아이템 AssetManager 스캔 base class는
  `/Script/RPGInventoryRuntime.RPGItemDefinition`이다.
- Item Backend 설정의 현재 섹션은 `/Script/RPGInventoryRuntime.RPGItemBackendSubsystem`이다.
  저장소의 실행 스크립트도 이 섹션을 사용한다. 별도 배포용 ini/명령행의 옛 섹션은 새 이름으로 갱신한다.
- DLL 경계가 바뀌므로 최초 적용은 Editor를 재시작하고 정상 UHT/UBT 빌드로 반영한다.

## 검증

`ProjectRPG.Item.Plugin.ExistingAssetsAndRedirects`는 기존 아이템 에셋의 검색·로드와 23개 타입 경로를 검사한다.
`ProjectRPG.Item.Plugin.ProjectionNotification`은 게임 클래스 없는 복제 수신 알림 경계를 검사한다.
`ProjectRPG.Item.Plugin.ProjectionCopyKeepsReceiver`는 리플렉션을 통한 데이터 복사 후에도
수신 객체의 알림 연결을 유지하고 템플릿에 알리지 않는지 검사한다.
기존 `ProjectRPG.Item.*` 검사는 동일한 공개 include와 실제 플러그인 구현을 사용한다.
기존 검사/실행 스크립트의 소스 해시 경로는 이동한 파일의 새 경로를 사용한다.
`Run-ItemAutomationTest.ps1`은 게임 DLL과 인벤토리 DLL 및 플러그인 소스의 불변성을 함께 확인한다.

2026-11-06 분리 구현 검증:

- 게시 `96081fe7`과 본인 변경만 포함한 독립 사본에서 UHT 및 Editor Development 전체 빌드 성공.
  최초 실행에서 장비 테스트 5개가 옛 클래스 경로의 검색 누락을 검출했고,
  검색 호환 수정 후 증분 빌드와 `ProjectRPG.Item.*` 155개 모두 성공했다.
- 기존 정의 에셋 12개 검색·로드, CoreRedirect 23개, 복제 수신 알림 검사를 통과했다.
  기존 가방 WBP를 포함한 에셋 13개의 파일 해시가 실행 전후 동일했다.
- 전체 검사 후 리플렉션 복사에서 로컬 알림까지 복사되지 않도록 `WithCopy=false`를 지정했다.
  이 최종 변경은 게임 모듈 없는 별도 프로젝트에서 플러그인 전체 UHT/Editor 빌드 및
  복사된 데이터·수신 알림 회귀 1개로 확인했다. 앞선 155개와 같은 바이너리의 재실행은 아니다.
- 독립 입력 및 추가 입력 2개를 대조했고, 원본 DLL 교체·에셋 저장·실제 백엔드 쓰기는 수행하지 않았다.
  이 결과는 headless native 검증이며 멀티플레이 실서버·패키징 검증 결과를 뜻하지 않는다.
- 자기 임시 사본·캐시·DLL/PDB 등 총 9,281,448,557 bytes를 폐기했다. 유지 예외는 없다.
  작은 로컬 근거는 `Saved/BuildChecks/InventoryPlugin-20261106/`에 보관하며,
  폐기한 바이너리의 해시는 당시 검증 기록으로만 사용한다.
