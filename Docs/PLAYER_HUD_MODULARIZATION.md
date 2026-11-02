# PlayerHUD 기능별 모듈화

## 2026-10-29 — 첫 단계: 스킬바 추출 완료

`WBP_PlayerHUD`의 기존 스킬 셀 네 개를 독립 `WBP_PlayerSkillBar`로 옮겼다.
부모는 `PlayerSkillBar` 인스턴스의 화면 배치만 소유한다. 스킬 슬롯의 내부 구성은
모듈에서 편집하며, 각 셀은 기존 공용 구현으로 편성·키 이름·아이콘·쿨타임을 읽는다.

```text
WBP_PlayerHUD
└─ PlayerSkillBar : WBP_PlayerSkillBar
   └─ HorizontalBox_SkillQuickSlots
      ├─ SkillQuickSlot_0 : WBP_SkillQuickSlot
      ├─ SkillQuickSlot_1 : WBP_SkillQuickSlot
      ├─ SkillQuickSlot_2 : WBP_SkillQuickSlot
      └─ SkillQuickSlot_3 : WBP_SkillQuickSlot
```

### 책임과 편집 위치

| 내용 | 소유 위치 |
|---|---|
| 스킬바의 화면 위치·크기·앵커·Z 순서 | `Content/Blueprints/UI/PlayerHUD/WBP_PlayerHUD.uasset`의 `PlayerSkillBar` |
| 표시할 네 슬롯·칸 사이 간격·셀 배치 | `Content/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerSkillBar.uasset` |
| 셀 외형·쿨타임 머티리얼 | 기존 `Content/Blueprints/UI/QuickSlot/WBP_SkillQuickSlot.uasset` |
| 편성·아이콘·키 이름·쿨타임·드래그/해제 | 기존 `URPGQuickSlotWidget`·`URPGQuickSlotViewModel`·`UQuickSlotComponent` |

모듈은 일반 `UUserWidget`을 부모로 사용한다. 새 CommonUI 활성화/입력 매핑·창 이동이나
게임플레이 상태를 추가하지 않는다. 중첩 UserWidget의 소유 PlayerContext 전달은 엔진의
`UUserWidget::Initialize`/`SetPlayerContext`를 사용한다. 각 셀의 기존 Construct/Destruct
구독·해제와 ViewModel 책임은 그대로다. 부모가 셀 번호나 내부 이미지/텍스트를 조회하지 않는다.

이번 단계의 네 칸은 모듈 디자이너에 작성된 구성이다. 런타임 가변 슬롯 목록/8칸 자동 생성,
자동 편성, 새 입력 키 추가까지 구현한 것은 아니다. 슬롯 수 확장은 화면·입력·기존 8칸 저장
계약을 함께 확인해 별도로 진행한다.

### 변경과 보존

- 기존 `HorizontalBox_SkillQuickSlots`와 네 자식을 부모에서 제거하고 같은 Canvas 자식 순서에
  `PlayerSkillBar` 하나를 넣었다. 너비 445와 기존 위치·높이·앵커·Z 순서를 유지했다.
- 네 셀의 클래스·인덱스 0~3·스킬 모드·입력 텍스트·드래그 설정을 포함한 속성과 슬롯 간격을
  비교했다. 추출 대상에 대한 부모 그래프 참조가 없음을 변경 전에 확인했다.
- 나머지 HUD 위젯 18개와 모든 기존 EventGraph/함수 노드·핀·연결을 변경 전후 비교했다.
  HP/MP·고유 능력·차지·알림·아이템·우편과 숨겨진 옛 스킬 행은 이번 범위에 포함하지 않았다.
- [추출 스크립트](../Scripts/Editor/ExtractPlayerSkillBar.ps1)는 기본 읽기 전용이며 `-Apply`에서만
  대상 두 WBP를 저장한다. PIE/미저장 HUD·기존 모듈 충돌을 확인하고 원본 에셋을 백업한다.
  완료된 구조에서는 재작성/저장 없이 종료한다.
- [기존 네 번째 슬롯 작성 스크립트](../Scripts/Editor/ConfigureGreatSwordFourthSkillSlot.py)는
  모듈 안의 셀과 부모의 모듈 배치를 구분하도록 수정했다. 모듈의 칸을 옛 작성기로 재생성하지 않는다.
  검증 출력 경로를 지정할 수 있어 과거 작성 증거를 덮어쓰지 않는다.

### 확인한 범위

- MCP 에디터에서 PIE=false/대상 clean 확인 후 두 WBP 작성·컴파일·저장 및 dirty=false.
- 부모의 실제 위젯 23개 → 19개, 모듈의 루트/셀 5개. 기존 다른 위젯 18개·그래프 보존.
- 추출 스크립트 재실행: 추가 수정·저장 없음.
- 독립 `UnrealEditor-Cmd`/UserDir에서 디스크 재로드, 두 WBP 컴파일 성공과 프로세스 종료 0.
  네 셀의 속성·간격, 모듈 배치, 나머지 위젯을 대조했고 저장 0/dirty=false/파일 해시 동일.
- 기존 네 번째 슬롯 스크립트의 모듈 읽기: `needs_change=false`.
- 공용 슬롯 두 에셋·입력 설정/매핑/R 액션·DrawHUD 에셋·슬롯 C++/ViewModel·공유 DLL 등
  보호 파일 10개 SHA256 동일. 새 C++/UBT/링크·GUI 종료·타 프로세스 종료 없음.

증거는 `Saved/BuildChecks/HudSkillBarModule-20261029/`의 `Before.json`, `Applied.json`,
`Repeat.json`, `ReloadResult.json`, `Reload-Final.log`, `ProtectedAfter.json`이다.
최초 두 재로드 검사는 MCP의 `{refPath: ...}`와 Python의 문자열 soft reference 표현 차이로
비교에 실패했다. 같은 경로를 정규화한 최종 검사만 성공이며 이전 로그를 보존했다.
이 보완 과정에서는 에셋을 다시 수정하지 않았다. 로그의 기존 기동/스키마 경고를
프로젝트 전체 경고 0으로 표현하지 않는다.

실제 플레이의 키 입력·쿨타임 표시·드래그/해제·다인 복제/GPU 화면 비교는 이번에 실행하지 않았다.
구조/설정 보존과 에셋 컴파일·재로드 성공을 실제 전투 실행 성공으로 계산하지 않는다.

## 공용 행동 진행 표시 계약 — 2026-10-29 사용자 요구

사용자는 현재 프로그레스바를 **홀딩·캐스팅·차지 스킬과 상호작용**에서 공용으로 사용할 것을
명시했다. 다음 모듈은 차지 전용 위젯이 아닌 `ActionProgress` 역할로 분리한다.
이 절은 실제 호출을 대조한 구현 기준이다. 공용 상태 API의 구현 범위는 아래 후속 절을 따르며,
기존 호출자와 독립 표시 위젯의 이관은 아직 남아 있다.

현재 `UPlayerUIComponent`에는 Show/Hidden·진행률·문구·시간·`OnChargeCompleted` 이벤트가
분리돼 있고 요청 식별자는 없다. `URPGPlayerGameplayAbility`의 표시/숨김/갱신 함수와
`URPGGameplayAbility_SkillContainer::ShowSkillProgress/HideSkillProgress/UpdateSkillProgress`
등이 이 경로를 사용한다. 부모 HUD는 이 이벤트들로 하위 상태바와 완충 연출을 직접 조작한다.

### 표시와 게임플레이의 책임

| 사용처 | 모듈에 전달할 표시 상태 | 호출자가 계속 담당할 판정 |
|---|---|---|
| 홀딩 | 유지 문구·경과/남은 시간·진행률, 필요시 종료 시간이 없는 표시 | 입력 유지/해제·틱 효과·자원 소모·종료 |
| 캐스팅 | 시전 문구·시전 진행률·남은 시간 | 시전 성공·중단·실제 효과 발동 |
| 차지 | 충전 진행률·충전 단계·완충 준비 연출 | 충전 단계 판정·버튼 해제·공격 실행 |
| 상호작용 | 대상/행동 문구·진행률·필요시 서버 확인 대기 | 거리/대상 유효성·서버 승인·완료/취소 |

위젯은 스킬 Ability나 상호작용 Actor의 구체 타입을 알아야 하지 않는다. 이름·아이콘·진행률·
선택적인 시간·단계·표시 상태를 받아 그리며, 실제 경과 시간과 완료/취소는 해당 행동의 원본에서
전달한다. 100% 도달이나 UI 타이머 종료로 스킬/상호작용 성공을 판정하지 않는다.
완충 준비는 행동 종료와 별개이며, 완충 상태에서 입력을 유지하는 표시도 가능해야 한다.

### 수명과 공통 인터페이스

- 공용 시작 요청은 한 번의 행동 실행을 식별하는 핸들을 발급하고, 갱신·준비 상태 변경·종료는
  같은 핸들을 요구하는 형태로 구현한다. 클래스나 스킬 태그만으로 반복 시전을 식별하지 않는다.
- 이전 행동 A의 늦은 갱신/종료가 새 행동 B의 표시를 변경하지 못하게 한다. 위젯 재생성·Pawn 교체
  뒤의 오래된 콜백과 같은 Ability의 재활성화도 구분한다. 기존 무식별 이벤트의 이름만 바꿔서는
  이 보장을 만들 수 없으므로 실제 생산자의 시작/종료 경로도 함께 이관해야 한다.
- 시작 상태는 문구·진행률·시간 표시 여부를 한 번에 전달한다. 이전 행동의 문구/수치가 새 행동에
  잠시 남지 않게 하며, 늦게 생성된 위젯은 현재 상태를 먼저 읽고 이후 변경을 구독한다.
- 시간 표시 없음/전체 시간 미확정, 증가/감소 진행, 일반 진행/완충 준비/종료 이유를 구분한다.
  0 이하의 전체 시간·비정상 수치를 나눗셈에 그대로 사용하지 않는다.
- 동시 표시의 선택은 공통 표시 상태 소유자 한 곳에서 결정한다. 여러 호출자가 위젯을 직접
  Show/Hide하지 않는다. 새 표시 선택이 이전 행동의 게임플레이 취소를 뜻하지 않는다.
- 모듈은 자기 연출 타이머/머티리얼/구독을 정리한다. 부모 HUD는 내부 ProgressBar·텍스트·머티리얼을
  건드리지 않는다. 기존 PlayerUIComponent와 책임을 통합하고 별도의 중복 진행 상태 관리자를 만들지 않는다.

구현 검증은 네 사용처의 표시 전환, 완충 뒤 입력 유지, 취소/피격 중단, A 종료 후 B 시작에 도착한
늦은 A 콜백, 같은 행동의 반복 실행, 위젯 재생성/Pawn 교체를 포함한다. 현재 게임플레이의 취소·
권한·자원 소비 규칙은 표시 모듈로 옮기지 않는다.

## 2026-10-30 — 공용 진행 상태 API

`UPlayerUIComponent`가 하나의 현재 표시 상태를 소유하며,
`UI/Progress/RPGActionProgressTypes.h`의 공용 자료형으로 홀딩·캐스팅·차지·상호작용을 표현한다.
기존 진행 이벤트와 실제 Ability/SkillContainer 호출, HUD 에셋은 이번 API 단계에서 이관하지 않았다.

| API | 동작 |
|---|---|
| `BeginActionProgress(Source, Data)` | 유효한 시작마다 새 GUID 핸들을 발급하고 표시 상태를 한 번에 교체한다. 이전 게임플레이를 취소하지 않는다. |
| `UpdateActionProgress(Handle, Data)` | 현재 실행의 핸들·유효한 원본·동일 종류일 때만 전체 표시 상태를 갱신한다. |
| `EndActionProgress(Handle, Reason)` | 현재 실행만 완료·취소·중단으로 종료한다. 늦은 이전 실행의 종료는 거절한다. |
| `GetActionProgressSnapshot()` | 현재 상태의 복사본을 반환한다. 늦게 부착된 위젯도 초기 표시를 복원할 수 있다. |
| `OnActionProgressChanged` | 변경 알림 후 현재 스냅샷을 다시 읽는다. Native 구독도 지원한다. |

상태에는 문구·선택 아이콘, 전체 시간 유무·경과 시간, 시간 표시 방식, 증가/감소 진행,
단계 및 `Running`/`Ready`/`AwaitingConfirmation`이 포함된다. 진행률은 0~1로 제한하지만
100% 도달·완충 준비가 자동 종료를 뜻하지 않는다. 비정상 수치·잘못된 단계·다른 종류로의
갱신은 기존 상태를 변경하지 않고 거절한다. 알림 도중 새 행동이 시작돼도 이전 요청의
후처리가 새 상태를 지우지 않으며, 시작 함수는 자신이 발급한 핸들을 반환한다.

### Tick과 수명 계약

`PlayerUIComponent`에는 진행 상태용 Tick이나 주기적 감시 타이머를 추가하지 않는다.
행동의 원본이 시간과 준비/완료 상태를 전달한다. 컴포넌트 등록 해제·EndPlay 및 Pawn의
Controller 변경 이벤트에서 표시를 무효화하며, 재등록·재소유 후 이전 핸들은 재사용하지 않는다.

원본 UObject는 약한 참조다. 원본 소멸은 상태 조회 시 숨기고 갱신을 거절하는 방어 경로이며,
임의 UObject 소멸만으로 변경 알림이 자동 발생하지는 않는다. 따라서 호출자 이관 시
Ability/Task/상호작용의 완료·취소·종료 경로에서 원본이 유효할 때 반드시 `EndActionProgress`를
호출하고 구독을 해제해야 한다. 사망/피격 중단 판정도 원본이 맡는다.
콜백은 **자신의 실행에서 받은 핸들**을 캡처해야 하며 같은 Ability의 최신 핸들을 뒤늦게 읽으면 안 된다.

현재 기존 무식별 Show/Hide/진행 이벤트는 그대로 남아 있으므로 기존 화면에 실행 격리 보장이
이미 적용된 것은 아니다. 실제 생산자 이관과 위젯의 초기 조회·구독/해제가 다음 연결 범위다.

### 검증 결과와 정리

- UHT 및 선택 CPP 컴파일 성공 후 분리 DLL에서 `ProjectRPG.UI.ActionProgress.*` 네 검사 모두 성공,
  UE/호스트 종료 0. 네 종류·100%/준비 상태 유지, 반복 실행/늦은 콜백/재진입,
  원본 소멸·소유권 변경·재등록, 비정상 수치와 상태 보존을 확인했다.
- 최초 링크는 다른 완료 작업의 로비 헤더와 기존 객체 불일치로 실패했다. 현재 로비 CPP를
  자체 임시 객체로 컴파일해 해소했으며 해당 원본 코드·공유 객체를 수정하지 않았다.
  첫 native의 3 Success/1 Fail은 테스트가 추상 `UObject`를 생성한 오류였다.
  테스트 원본을 구체 `USceneComponent`로 수정한 최종 단일 DLL의 4 Success와 구분한다.
- 소스 7개·보호 6개와 링크 입력 453개 전후 해시를 확인했다. 기동 Error 15줄은 남아 있고
  검사 시작 이후 Error는 0이다. 기존 미빌드 로비 테스트 2개는 분리 링크에서 제외했으며
  전체 타깃 통합·다른 기능 회귀·실제 스킬/상호작용·UI/GPU/네트워크 검증으로 확대 해석하지 않는다.
- HUD 에셋은 중단 중 외부 변경을 감지해 최초 재시도를 거절했다. `ExternalChanges.json`에
  이전/현재 해시를 남기고 현재 에셋을 보존했으며 이번 실행에서 HUD 에셋을 수정하거나 검증하지 않았다.
- 성공/실패 시 `finally` 정리로 이번 API 검증의 임시 파일 합계 **1,021,593,880 bytes(974.27MiB)**를
  폐기했다. OBJ/PCH 입력 사본은 만들지 않았다. 최종 `TempFinal`·분리 DLL/PDB·실행 프로젝트/캐시는
  남아 있지 않으며, 해시는 당시 증거다. 같은 바이너리 재실행에는 다시 빌드해야 한다.
- 정리 예외: 첫 실패의 오류 보고 프로세스가 종료 후 늦게 남긴
  `Temp/User/Saved/Crashes/UECC-Windows-ED77905C424526DFB54BCCB2B8CA1B38_0000/CrashContext.runtime-xml`
  **143,377 bytes**. 재귀 및 파일 단위 삭제가 자동 승인 검토에서 `blocked by policy`로 거절돼
  보존했다. HUD 담당의 미정리 예외이며 정책상 삭제가 허용되는 정리 시점에 처리한다.

작은 결과/명령/소스·모듈 해시와 필요한 실패/최종 로그는
`Saved/BuildChecks/ActionProgressState-20261029/`의 `Result.json`, `Native.log`,
`SourceHashes.json`, `Protected.json`, `LinkInputHashes.json`, `Attempt*-Result.json` 등에 있다.
공유 UHT/UBT/Intermediate 예약은 해제했다. 원본 DLL에는 이번 변경을 링크하지 않았다.

## 2026-10-30 — 독립 진행 표시 위젯

`URPGActionProgressWidget`은 공용 상태 API를 구독하는 독립 `UUserWidget`이다.
실제 스킬 호출부는 `training-charge-release-20261030` 담당이 같은 Policy/SkillContainer 파일을
수정 중이어서 동시 편집하지 않고 표시 모듈을 먼저 구현했다. 기존 HUD 에셋·호출자와 공용 상태 API는
이번 단계에서 변경하지 않았다. 이 위젯이 현재 `WBP_PlayerHUD`에 이미 배치됐다는 뜻은 아니다.

### 사용과 책임

1. HUD는 위젯을 생성하고 `SetProgressSource(PlayerUIComponent)`로 표시 원본을 전달한다.
   Construct 전 지정도 가능하며, Construct 때 현재 상태를 읽고 변경을 구독한다.
2. Pawn이 바뀌면 HUD가 새 원본을 지정한다. 이전 원본 구독은 해제하고 새 원본의 상태를 즉시 읽는다.
   원본을 찾지 못하면 `SetProgressSource(nullptr)`로 비운다. 위젯은 Pawn을 주기적으로 탐색하지 않는다.
3. Destruct 때 구독·표시·아이콘 스트리밍을 정리한다. 같은 위젯을 다시 부착하면 지정한 원본의
   최신 상태를 읽는다. 제거/교체는 원본 행동을 취소하거나 종료하지 않는다.

네 행동 종류는 같은 표시 경로를 사용한다. 확정된 시간은 전달된 비율을 그리고, 전체 시간이 없으면
UMG marquee 연출을 사용한다. 시간·단계·아이콘은 값이 있을 때만 보이며, `Ready`와
`AwaitingConfirmation` 문구는 종료와 별개로 유지한다. 비활성 상태에서는 위젯 전체를 숨기고
이전 문구/수치를 비운다. 자체 NativeTick·게임플레이 타이머나 성공/취소 판정을 추가하지 않았다.

### 외형 확장

기본 네이티브 레이아웃은 아이콘·행동명·바·시간·단계·상태 문구를 제공한다.
이 클래스를 부모로 하는 WBP에서는 같은 이름의 `BindWidgetOptional` 컨트롤로 외형을 작성할 수 있다.

| 선택 컨트롤 이름 | 타입 | 내용 |
|---|---|---|
| `ProgressBar` | ProgressBar | 진행률/미확정 시간 marquee |
| `ActionLabel` | TextBlock | 행동 문구 |
| `TimeLabel` | TextBlock | 전달된 경과/남은 시간, 1자리 소수 |
| `StageLabel` | TextBlock | 현재 단계 / 전체 단계 |
| `PhaseLabel` | TextBlock | 준비/서버 확인 대기 문구 |
| `ActionIcon` | Image | 선택 soft texture, UImage 비동기 로드/취소 사용 |

`OnProgressPresentationChanged`는 추가 외형 연출용 BP 이벤트다. 여기서
`GetDisplayedProgress()`로 현재 스냅샷을 읽을 수 있다. 위젯 내부를 부모 HUD가 직접 조회하지 않는다.
아이콘 변경/비우기 때 이전 스트리밍과 브러시를 먼저 지우며, UImage의 이전 경로 콜백 차단을 재사용한다.
빠진 선택 컨트롤은 허용한다. 별도 BP 연출을 추가한다면 Destruct에서 그 연출의 구독/타이머도 정리한다.

### 확인한 범위

- UHT/선택 CPP 컴파일·분리 링크 성공. 첫 시험 컴파일의 UProgressBar getter 이름 오류는
  엔진의 `UseMarquee()`로 수정했으며 제품 소스 컴파일과 구분해 기록했다.
- `ProjectRPG.UI.ActionProgressWidget.Presentation` 및 `BindingLifetime` 두 native 검사 성공,
  UE/호스트 종료 0. 실제 UMG/Slate 객체에서 늦은 부착·네 종류/준비/대기·비율/선택 표시·이미 로드된
  아이콘 교체와 원본 교체/이전 원본 이벤트 차단·제거/재부착·등록 해제·명시적 해제를 확인했다.
- 소스 10개·보호 3개 전후 해시 동일, 실제 분리 모듈 로드/해시 확인. 기동 Error 15줄은 남아 있고
  검사 시작 이후 Error는 0이다. 다른 담당의 미빌드 테스트 4개는 분리 링크에서 제외했다.
- 실제 HUD 배치·작성 WBP 재부모화/연출·실제 비동기 아이콘 로드 경합·GPU·전투/상호작용·네트워크·
  정상 원본 모듈 통합은 이번 검증 범위가 아니다. 기존 API 검사 4개는 반복하지 않았다.
- 자체 검증 프로세스 종료 후 `finally`에서 **415,864,361 bytes(396.60MiB)**를 자동 폐기했다.
  Temp의 DLL/PDB·OBJ·실행 프로젝트/UserDir·캐시는 남아 있지 않고 이번 작업의 정리 예외는 없다.
  원본 DLL/에셋과 다른 담당 입력은 유지했다. 폐기된 바이너리 해시는 당시 증거이며 재실행에는 재빌드가 필요하다.

증거: `Saved/BuildChecks/ActionProgressWidget-20261030/{Result.json,Native.log,SourceHashes.json,
Protected.json,LinkInputHashes.json}` 및 작은 컴파일/링크 로그. 공유 UHT/UBT/Intermediate 예약은 해제했다.

## 2026-10-30 — HUD 생성/회수와 소유 Pawn 연결

`ARPGHUD`가 로컬 플레이어 화면에 `URPGActionProgressWidget`을 하나 생성하고,
현재 소유 Pawn의 `UPlayerUIComponent`를 표시 원본으로 지정한다. 화면 배치는 HUD가 담당하며
현재 네이티브 기본값은 420×160, 앵커 (0.5, 0.75), 정렬 (0.5, 0), Z=5다.
위젯 내부 컨트롤에는 접근하지 않는다. 표시 상태가 없으면 모듈 전체가 숨겨져 입력을 가로채지 않는다.

- 현재 Controller가 로컬 플레이어/월드의 실제 Controller인지 확인한 후 `AddToPlayerScreen`을 사용한다.
  부착 실패 시 생성한 위젯을 회수한다. 기존 기여/훈련/메뉴 위젯 생성 경로는 유지했다.
- 서버와 클라이언트에서 호출되는 `OnPossessedPawnChanged`를 구독한다. 콜백의 인수만 믿지 않고
  현재 Controller/Pawn을 다시 읽어 재진입 이후의 소유권을 사용한다. 상시 Tick/폴링은 없다.
- Controller의 Pawn 포인터가 Pawn의 Controller 정보보다 먼저 복제될 수 있으므로,
  두 포인터의 즉각적인 상호 일치를 원본 지정의 필수 조건으로 두지 않는다. 로컬 Controller가
  현재 가리키는 같은 월드의 Pawn에서 원본을 찾고, 원본 컴포넌트의 Controller 수명 무효화를 재사용한다.
- Pawn 교체/해제 시 이전 원본 구독과 표시를 정리한다. HUD EndPlay에서는 Controller 이벤트 구독을
  해제하고 원본을 비운 뒤 위젯을 화면에서 제거한다. HUD 교체는 행동 자체를 취소하지 않는다.
- 원본은 Pawn 생성 시 존재하는 UI 컴포넌트를 기준으로 찾는다. Pawn 변경 이벤트 이후 새 UI 컴포넌트를
  동적으로 설치하는 흐름은 별도 연결이 필요하다. 실제 Player의 기존 UI 기본 컴포넌트는 이 조건에 맞는다.

이번 연결은 **네이티브 HUD가 새 표시 모듈을 생성하고 원본을 연결하는 단계**다.
`WBP_PlayerHUD`의 기존 바/그래프나 스킬 호출부를 교체하지 않았으며 원본 DLL도 링크하지 않았다.
차지 담당의 활성 Policy/SkillContainer 예약 종료/인계 뒤 실제 실행별 핸들 생산자를 연결하고
기존 무식별 표시 경로를 제거해야 한다. 기존 스킬이 이미 새 바에 표시된다고 해석하면 안 된다.

### 검증과 인계

- UHT/선택 CPP 컴파일·분리 링크 후 `ProjectRPG.UI.ActionProgressHUD.ViewportPossessionLifecycle`
  성공, UE/호스트 종료 0. 실제 로컬 Player/가상 창/플레이어 Slate 레이어에서 제품 HUD가 위젯을
  하나 생성하고 현재 행동을 읽는 것, Pawn 교체·이전 원본 이벤트 차단·해제/재소유,
  HUD 종료의 구독/화면 회수·행동 유지·새 HUD의 단일 생성/현재 상태 복원을 확인했다.
- 엔진 `SetPawnFromRep`→`OnRep_Pawn` 경로에서 Controller의 Pawn 포인터가 먼저 도착한 경우와
  복제된 해제의 원본/구독 정리도 확인했다. 실제 네트워크 패킷/Iris 검사는 아니다.
- 최초 검사는 테스트 월드의 `AreActorsInitialized=false` 때문에 Actor의 동적 이벤트가 억제돼
  실패했다. fixture를 정상 초기화한 성공과, 이후 복제 순서 조건을 보완한 최종 성공을 별도 기록했다.
- 최종 소스 13개/보호 3개·링크 입력 456개 전후 해시 확인. 기동 Error 15줄/검사 시작 이후 Error 0이며
  기존 에셋 로딩 경고는 남아 있다. 다른 담당의 미빌드 테스트 4개와 작업 중 새 훈련 GameMode 객체/등록은
  자체 분리 링크에서 제외했다. 생성 묶음은 임시 사본에서 해당 등록 include만 제외했고 원본 생성 파일은 유지했다.
  전체 현재 타깃 통합·훈련 콘텐츠·실제 스킬 표시·GPU 검증으로 확대 해석하지 않는다.
- 세 실행의 Temp 합계 **1,256,238,459 bytes(약 1.17GiB)** 자동 폐기, 잔여/정리 예외 없음.
  원본 DLL/에셋과 다른 담당 프로세스/검증 입력은 유지했다. 폐기된 바이너리는 다시 빌드해야 실행할 수 있다.

작은 결과/해시/명령·필수 로그: `Saved/BuildChecks/ActionProgressHUD-20261030/Result.json`,
`Native.log`, `SourceHashes.json`, `Protected.json`, `LinkInputHashes.json` 및 `Attempt1/2-*` 기록.
공유 UHT/UBT/Intermediate·본인 소스 예약은 해제했다. 완료 시점의 로드맵에서
`training-charge-release-20261030`도 완료/예약 해제를 확인했다. 다음 단계는 그 최종 변경을 보존하면서
실제 Policy/SkillContainer의 실행별 핸들을 연결하는 작업이며, 착수 시 최신 기록을 다시 확인한다.

## 2026-10-30 — 실제 실행 정책의 공용 진행 핸들 연결

현대 실행 정책 `Charge`·`Holding`·`Casting`이 기존 Host의 무식별 진행 이벤트 대신
`UPlayerUIComponent`의 공용 시작/갱신/종료 API를 호출한다. 각 정책은 활성화마다 생성되는
객체이므로 자신의 UI 약참조·핸들·표시 입력만 보관한다. Ability의 최신 실행 핸들을 다시 읽지 않는다.
공용 컴포넌트가 현재 표시를 선택하는 책임과 게임플레이 판정은 그대로다.

- 로컬 제어 Pawn의 UI 컴포넌트에만 표시한다. Dedicated/비로컬 실행이나 UI가 없는 경우에도
  기존 게임플레이 실행은 계속되며 UI를 기다리지 않는다.
- 시작 시 종류·전체 시간·0 진행률·단계 수·아이콘·문구를 한 번에 전달한다. 시간은 기존 정책의
  스탯 배율을 적용한 값이다. 문구는 정책의 현지화된 표시 이름(Charge/Holding/Casting),
  아이콘은 동결된 RuntimeSpec을 사용한다. 스킬 고유 표시 이름은 현재 RuntimeSpec에 없으므로 별도 메타데이터 연결 범위다.
- 차지는 현재/최대 단계를 함께 갱신하고 완충 시 `Ready`를 유지한다. 실제 해제·자동 해제나
  취소/종료에서 해당 핸들만 닫는다. 완충 해제의 훈련 관측 사실을 보관하는 기존 플래그는 유지했다.
- 홀딩은 기존 성공 구간을 `Ready`로 표시하고 입력 해제/기존 자동 판정에서 표시를 닫는다.
  캐스팅은 완료·입력 취소·몽타주 실패/중단에서 표시를 정리한다. 효과·피해/훈련 승인 판정은 UI로 옮기지 않았다.
- 표시 알림 도중 동기 취소돼 시작 함수가 아직 핸들을 반환하지 못한 경우에도, 반환 후 그 시작의
  핸들을 정리한다. 종료 전에 보관한 핸들과 원본을 먼저 비워 새 행동을 시작하는 콜백이
  이전 후처리로 지워지지 않게 한다. 알림 뒤의 재진입/설정 초기화를 확인하고 이전 설정 포인터를 다시 사용하지 않는다.

착수 당시 훈련 GameMode 검증이 기존 소스/링크 입력을 고정하고 있어 먼저 자체 Staging에서 작성했다.
해당 담당의 03:14 KST SourceAudit 및 소스/입력 예약 해제 기록을 확인하고, 원본 Policy 두 파일의
해시가 작성 시작 전과 같은지 대조한 뒤 적용했다. 공유 UHT/UBT/Intermediate를 쓰지 않고 자체 manifest의
Project_RPG 생성 코드 출력과 헤더를 분리했으며, 다른 모듈의 생성 코드 저장은 끈 상태로 검증한다.

이 단계는 세 실행 정책의 실제 생산자 이관이다. 기존 `URPGSkillAction_Charge` fallback과
PlayerGameplayAbility/BP의 옛 표시 함수·`WBP_PlayerHUD` 그래프는 아직 남아 있다.
새 세 정책은 그 이벤트를 호출하지 않으므로 기존 그래프가 새 정책 진행률을 갱신하지 않는다.
레거시 경로의 최종 제거와 작성 외형/스킬 고유 문구·배치 대조, 정상 모듈 반영은 후속이다.

### 검증과 생성 코드 인계

- 독립 manifest의 UHT 출력 692개, 생성 묶음/실제 Policy/시험 CPP 6개 CL·분리 모듈 링크 성공.
  최종 단일 DLL에서 새 `ProjectRPG.UI.SkillActionProgress.*` 3개와 기존
  `ProjectRPG.Progression.TrainingCharge.*` 2개, 합계 **5개 모두 Success**, UE/호스트 종료 0.
- 실제 정책과 독립 월드/타이머·로컬 Pawn/UI에서 완충 유지/단계/해제, 홀딩 성공 구간,
  캐스팅 완료/입력 취소/몽타주 실패, 이전 정책의 늦은 갱신/종료, 시작/Ready 알림 중 취소와
  RuntimeSpec 초기화를 확인했다. Host의 타깃/몽타주/VFX 서비스는 모형이며 실제 GAS/전투/네트워크 검사는 아니다.
- 기존 차지의 기본 부분 해제·완충 해제·해제 시점 계산·취소/재사용·설정 초기화 후 관측 결과도 통과했다.
  이 검증에서 세 현대 정책의 옛 Show/Hide/Update/완료 Host 호출은 0이다.
- 최초 실행기의 .NET 선택과 링크 응답 개행 오류, 새 fixture의 로컬 플레이어 누락은 실패 증거를
  보존하고 보완했다. 최종은 5개 전체 성공이며 기동 Error 15줄/검사 시작 이후 Error 0이다.
- 적용 소스/관련 입력 7개·보호 3개·링크 입력 462개 해시 확인. 검증한 Staging 3개가 적용된
  실제 소스와 같은 바이트인지 확인한 뒤 사본을 폐기했다. 사용 당시 입력 10개의 해시는
  `VerificationInputHashes.json`, 현재 실제 파일 7개는 `SourceHashes.json`이다.
- 네 실행의 Temp와 작성 사본 합계 **953,234,919 bytes(약 0.89GiB)** 폐기, 잔여/정리 예외 없음.
  작은 결과/명령/해시·실패/최종 로그와 실행 스크립트만 `Saved/BuildChecks/ActionProgressProducer-20261030/`에 남겼다.
  폐기한 바이너리/입력은 당시 증거이며 같은 DLL 재실행에는 재빌드가 필요하다.

**다음 빌드 인계:** 이번에 바뀐 Policy 헤더는 자체 UHT 출력으로 검사했으며 공유 생성 파일을 갱신하지 않았다.
새 헤더의 `GENERATED_BODY` 위치/클래스 크기가 달라졌으므로 다음 정상 UBT/UHT 또는 별도 검증의
자체 UHT 생성이 필요하다. 이전 공유 generated 헤더/Policy OBJ와 새 헤더를 섞어 컴파일하지 않는다.
원본 DLL/사용자 GUI는 유지했고 이번 변경을 정상 모듈에 반영하지 않았다. 본인 소스 예약은 해제했다.

## 2026-10-30 — 레거시 차지와 부모 HUD 정리

레거시 `URPGSkillAction_Charge`도 자기 UI 약참조·실행 핸들로 시작/갱신/Ready/해제/취소/종료를
전달한다. 작성된 스킬명·아이콘, 전체 충전 시간과 현재/최대 단계를 사용하며 완충 후 대기는
표시 종료와 구분한다. 이전 액션의 늦은 종료와 시작 알림 중 취소는 새 행동을 지우지 않는다.
기존 차지 타이머/단계·몽타주·VFX·게임플레이 이벤트 경로는 재사용하고 중복 해제만 차단했다.

부모 `WBP_PlayerHUD`의 옛 진행 위젯과 이벤트/연출 경로를 정리했다.

- 진행 이벤트 6종과 복제된 미연결 CustomEvent, 진행 전용 머티리얼/밝기·타이머 노드 등
  EventGraph 노드 **72개**, `BrightProgressBar` 함수, `WBP_ProgressBar` 한 개를 제거했다.
- 초기화의 Rage 바인딩→고유 능력 바인딩 사이 실행 연결 한 곳을 이어 나머지 초기화를 유지했다.
  진행 분기에서만 사용한 NoticeText getter 한 개도 제거했으며 실제 알림 위젯/독립 알림 그래프는 유지했다.
- 다른 위젯 **14개**의 속성/슬롯·배치, 남은 모든 기존 EventGraph 노드/핀/연결과
  `UpdateValue` 함수 전체가 기대한 연결 변경 외에 동일함을 확인한 뒤 저장했다.
- 직접 참조자 BP_Player/GA_Player_DrawHUD에 제거한 밝기 함수 호출이 없음을 확인했다.
  [정리 스크립트](../Scripts/Editor/RetirePlayerHUDLegacyProgress.ps1)는 기본 읽기 전용이며,
  `-Apply`에서만 이 에셋을 저장한다. PIE/미저장 HUD·다른 기능 침범·외부 함수 호출을 거절하고
  완료 구조에서는 추가 수정/저장 없이 종료한다.

옛 PlayerUIComponent 델리게이트와 PlayerGameplayAbility/SkillContainer의 C++ 호환 함수는 남아 있다.
현대 정책 3종과 레거시 차지 액션은 그 진행 표시 경로를 호출하지 않으며 부모 HUD도 구독하지 않는다.
다른 기존 에셋의 호환 필드를 삭제하는 작업은 이번 범위에 넣지 않았다.

### 정상 컴파일과 통합 검증

- 정상 Editor 타깃의 UHT/전체 NoLink 컴파일 성공. 이전 단계의 Policy 헤더와 공유 generated 헤더/
  객체 사이 차이는 이번 정상 UHT/컴파일에서 반영했다. 현재 공유 Intermediate를 이전 단계의
  갱신 전 입력과 혼동하지 않는다. 원본 Binaries DLL은 실행 중 GUI 때문에 아직 교체하지 않았다.
- 정상 응답/객체로 분리 링크한 DLL에서 API 4개·표시 위젯 2개·HUD 1개·현대 정책 3개·
  레거시 차지 1개·차지 훈련 2개, **13개 모두 Success/UE 종료 0**. 레거시 입력/자동 해제,
  완충 유지·단계·이전 액션 차단·시작 중 취소도 포함한다. 기동 Error 15줄/검사 시작 이후 Error 0.
- HUD 정리 재실행은 무변경/저장 0이다. 새 Cmd/정상 생성 코드의 분리 DLL에서 저장된 HUD를
  다시 로드해 14개 위젯의 모든 지원 속성/슬롯과 저장된 노드 목록, 밝기 함수 제거를 대조하고
  컴파일 `BS_UP_TO_DATE`·dirty=false·저장 0·디스크 해시 동일을 확인했다.
- native와 재로드는 별도 링크 실행이며 DLL 해시는 다르다. 두 링크의 **입력 465개 해시가 동일**하고
  소스 12개/원본 DLL과 수정 HUD 보호 해시도 확인했다. 같은 단일 바이너리의 재실행이라고 표현하지 않는다.
- 최초 시험의 protected API 사용, 자동 해제 fixture의 pending 타이머 프레임, 재로드 스크립트의
  임시 프로젝트 경로/Blueprint Python 조회 API 오류는 보완·실패 로그 보존 후 최종 성공과 구분했다.
  실제 전투/네트워크·GPU 외형 비교는 하지 않았다.
- 자체 DLL/PDB·실행 프로젝트/UserDir/캐시 등 **1,481,041,325 bytes(약 1.38GiB)** 자동 폐기,
  Temp 잔여 없음. 자동 승인 검토가 원본 복구 백업 `HUD-before.uasset` **480,021 bytes**의
  명시적 삭제를 `blocked by policy`로 거절해 이 파일만 본인 정리 예외로 보존했다.

작은 증거: `Saved/BuildChecks/ActionProgressLegacy-20261030/Completion.json`,
`Result.json`의 최종 native 13개 결과, `ReloadRun.json`/`HUDReload.json`의 재로드 성공,
`HUDBefore.json`/`HUDApplied.json`/`HUDPlan.json`/`HUDRepeat.json` 및 해시·필수 실패/성공 로그.

### 원본 DLL 반영 상태

앞선 검증 완료 시점에는 사용자 에디터 PID 27576이 이전 DLL을 사용 중이어서 원본 반영을 기다렸다.
사용자의 에디터 종료 통보 후 실제 종료를 확인하고 아래 후속 절의 정상 빌드/원본 재로드까지 완료했다.
검증 DLL을 다음 작업까지 보관하거나 사용자 프로세스를 임의 종료하지 않았다.

## 2026-10-30 — 정상 Editor DLL 반영 완료

사용자가 에디터를 닫았다고 알려 준 뒤 GUI/Cmd/빌드 프로세스가 없고 최신 예약이 겹치지 않는지 확인했다.
관련 소스 12개·HUD/기존 DLL과 이전 링크 입력 465개 모두 변경이 없었다. 정상
`Project_RPGEditor Win64 Development` 빌드가 종료 0으로 성공했고, 재컴파일 0·종속 모듈 포함
링크 34개·target 메타데이터 갱신으로 원본 Binaries와 정상 종속 모듈을 반영했다.

- 실제 설치된 `Binaries/Win64/UnrealEditor-Project_RPG.dll`의 SHA256은
  `4BCAB2FBBBCD73B2DB606B7920DCB24E76A59A559C1AD311ED2BB35CD7B764E7`이다.
- 원본 프로젝트의 새 Cmd 프로세스가 **그 원본 DLL 경로**를 로드했고 프로세스 종료 0,
  전후 DLL/관련 소스·HUD 해시 동일을 확인했다. 별도 RuntimeProject/검증 DLL을 로드한 결과가 아니다.
- 저장된 HUD의 위젯 14개 속성/배치와 노드 목록이 일치하고 기존 진행 바/밝기 함수가 없으며,
  컴파일 성공·dirty=false·추가 저장 0이다. 기존 native 13개는 동일 입력에서 통과했으므로 반복하지 않았다.
- 이번 자체 Temp UserDir/캐시 132,813 bytes를 `finally`에서 폐기했다. 원본 DLL/PDB·정상 빌드 출력은
  최종 결과물로 유지한다. 앞선 백업 삭제 거절 예외는 이번 단계에서 다시 시도하지 않았다.
- 기록: `Saved/BuildChecks/ActionProgressInstall-20261030/{BuildSummary.json,Result.json,
  InstalledHUDReload.json,Build.log,Reload.log}`. 설치/검사 완료 후 공유 빌드/입력 예약을 해제했다.

다음 GUI 실행부터 새 코드와 정리된 HUD를 사용한다. GUI 재시작·GPU 화면/실제 전투 입력·네트워크 검사는
이번 원본 반영 단계에서 실행하지 않았다. 훈련 E2E 담당은 이전 activation 사본 대신 현재 정상
DLL/생성 코드/객체와 HUD를 새 기준으로 확인하고 입력을 고정할 수 있다.

## 2026-10-30 — 스킬 고유 이름과 진행 위젯 설정

스킬 실행 시 `FRPGSkillRuntimeSpec.DisplayName`에 Definition의 `SkillName`을 복사한다.
이름이 비었을 때만 기존 DataTable 행의 이름을 사용하며, `Reset()`에서 비운다.
현대 홀딩/캐스팅/차지 정책과 레거시 차지는 이 이름을 시작 시 진행 데이터에 보관한다.
진행 중 Definition이나 실행 사양의 이름이 바뀌어도 현재 표시 문구는 바뀌지 않는다.
이름이 없는 현대 정책은 기존 행동 종류 문구를 사용한다.

HUD 자식 Blueprint의 `Widgets > Action Progress`에서 다음 항목을 설정할 수 있다.

| 항목 | 기본값 / 동작 |
|---|---|
| `ActionProgressWidgetClass` | `URPGActionProgressWidget`; 작성한 자식 WBP로 교체 가능, None이면 생성하지 않음 |
| `ActionProgressSize` | 420 × 160 |
| `ActionProgressAnchor` | (0.5, 0.75) |
| `ActionProgressAlignment` | (0.5, 0) |
| `ActionProgressOffset` | (0, 0), 앵커 기준 레이아웃 단위 |
| `ActionProgressZOrder` | 5 |

내부 외형은 진행 위젯이 담당하고 화면 배치는 HUD가 담당한다. 기존 기본 외형은 유지했으며,
이번에는 새 WBP나 스타일 에셋을 만들지 않았다. 기존 선택 바인딩 이름과
`OnProgressPresentationChanged`로 작성한 외형을 연결할 수 있다.
추가 Tick/타이머는 없다. 유효하지 않은 크기/좌표는 뷰포트에 등록하지 않는다.

작성 위젯의 Initialize/Construct에서 HUD가 종료될 수 있으므로 실제 뷰포트 등록 전에
Slate 위젯을 구성하고 소유자/월드/종료 상태를 다시 확인한다. Construct에서 종료된 HUD의
늦은 Canvas 삽입과 후속 안내 위젯 생성을 막는다. 위치/크기 setter가 앵커를 초기화하는
엔진 동작에 맞춰 위치·크기를 먼저 적용하고 앵커를 나중에 지정한다.

### 검증과 반영 경계

- 정상 Editor 타깃 UHT/전체 NoLink 컴파일 성공 후, 실제 UMG 텍스트 검증 보강과 앵커 수정의
  증분 컴파일도 종료 0이다. 공유 generated 헤더/객체는 새 구조체와 HUD 필드에 맞게 갱신했다.
- 첫 분리 DLL 실행은 15개 중 14개 Success이며 앵커 검사 한 개 Fail이다. 새 이름 사례는
  실행 전 이름 복사·중간 변경 차단·Reset·DataTable 대체와 실제 `UTextBlock` 문구를 확인했다.
  레거시 이름 복사와 기존 API/위젯/정책/훈련 차지 회귀도 통과했다.
- 앵커 순서 수정 후 영향받는 HUD 두 사례만 다시 실행해 모두 Success/종료 0이다.
  실제 가상 플레이어 Slate 레이어의 선택 클래스/크기/앵커/정렬/위치/Z순서,
  None·잘못된 크기·Construct 중 HUD 종료/잔여 등록·기존 소유 Pawn 교체를 확인했다.
  15개 전체를 단일 최종 DLL에서 재실행한 결과와 구분한다.
- 두 링크 입력은 각 466개이며 변경은 `RPGHUD.cpp.obj` 한 개뿐이다. 최종 선택 소스 12개와
  원본 DLL/HUD 보호 파일 2개 불변을 확인했다. 별도 새 Cmd에서 같은 최종 분리 DLL로
  저장된 WBP의 위젯 14개/속성·슬롯/노드 목록 보존, 컴파일 성공·dirty=false·저장 0도 확인했다.
- 성공/실패 실행의 자체 DLL/PDB·RuntimeProject/UserDir/캐시 합계 **704,553,401 bytes
  (671.91MiB)**를 `finally`에서 자동 폐기했다. 이번 Temp 잔여/대형 보존 예외는 없다.
  앞선 작업의 삭제 거절 예외는 다시 시도하지 않았다. 작은 결과/명령/해시·필수 로그만
  `Saved/BuildChecks/ActionProgressPresentation-20261030/`에 남기며 폐기 DLL 해시는 당시 증거다.
- 당시 사용자 GUI29992는 이전 정상 DLL `4BCAB2FB…7B764E7`을 사용 중이었다. 당시 이름/설정 필드는
  **정상 DLL 반영 전**이었다. 이후 정상 모듈 포함 여부는 아래 2026-10-31 검증에서 확인했다.
  실행 중 GUI에 새 reflected 필드를 Live Coding으로 적용하지 않는다. 실제 전투 입력·GPU·
  네트워크 및 작성 외형의 화면 검사는 이번 범위에 포함하지 않았다.

## 2026-10-31 — 설치된 정상 모듈의 진행 표시 통합 확인

재개 시 공용 소스·정상 빌드/입력은 `platform-player-security-20261031` 담당이 예약 중이었다.
이전 DLL을 다시 설치하지 않고 현재 설치 DLL과 이전 구현 소스 12개의 해시를 읽었다.
소스 12개는 이전 구현과 동일했으며 DLL은 다른 담당의 후속 정상 빌드로 갱신된 상태였다.

현재 설치 DLL **`9968DDA8ACD89AA21B8EB76D2099E7EDB5EE27D8B09F92F7A8A946532EA32034`**와
프로젝트/플러그인 DLL·manifest·descriptor/Config를 독립 RuntimeProject로 복사했다.
113개 파일의 복사 전후/전체 사본 생성 후 원본 해시를 대조해 혼합 사본을 거절하며,
실행 중 고정한 입력은 이 사본이다. 원본 DLL·Source/Intermediate를 고정하거나 변경하지 않는다.
Content/플러그인 콘텐츠·Resources는 읽기 링크이며 원본 에셋을 저장하지 않는다.

- 같은 사본 DLL의 집중 native **15개 모두 Success**, UE 종료 0이다. 지난 앵커 수정 후
  나누어 검사했던 15개를 이번에는 단일 설치 DLL 사본에서 함께 통과했다.
  API 4·위젯 2·HUD 2·현대 정책 4·레거시 차지 1·훈련 차지 2개다.
  14개 무경고, 배치 설정 사례 한 개는 프로젝트 에셋 로드 경고 139개를 포함한 성공이다.
  기동 Error 15줄과 탐색 이후 Error 0을 구분한다.
- 같은 DLL을 실제 로드한 새 Cmd의 Python 검사도 종료 0이다. 네이티브 `DisplayName` 필드와
  HUD 위젯 클래스·크기·앵커·정렬·위치·Z순서의 기본값을 확인했다. 저장된 PlayerHUD의 위젯 14개·
  스킬바 모듈/옛 바·밝기 함수 제거 상태, 컴파일 성공·dirty=false·저장 0을 확인했다.
  10-30 당시의 모든 속성/노드 보존 대조를 이번에 다시 수행한 것은 아니다.
- 113개 사본 입력과 원본 HUD/스킬바 해시는 실행 전후 동일했다. 실제 로드 경로는 자기
  RuntimeProject의 DLL이며 원본 DLL 직접 실행으로 표현하지 않는다. 이번에는 새 빌드·
  원본 DLL 교체·에디터 종료 없이 **이름/설정 기능의 정상 모듈 포함 대기를 해소**했다.
- [집중 실행 스크립트](../Scripts/Tests/Run-ActionProgressTests.ps1)와
  [리플렉션·재로드 검사](../Scripts/Tests/Verify-ActionProgressPresentation.py)를 유지한다.
  전자는 필요한 15개 이름/JSON 결과·완료 로그·종료 코드·실제 DLL 로드 경로/해시를 확인하며,
  실패/제한시간 종료에도 자기 프로세스의 사용 해제 후 `finally`에서 자기 Temp를 정리한다.
  경계 밖 삭제 거절·사용 중 파일의 삭제 전 거절·Junction 대상 보존도 작은 fixture로 확인했다.
- 실행 Temp/DLL·RuntimeProject/UserDir/리포트/캐시 **566,040,559 bytes(약 539.82MiB)**와
  정리 fixture 13 bytes를 자동 폐기했다. Temp 잔여/대형 예외 0, 링크 11개 해제이며
  작은 결과·실행 명령·해시·필수 로그만 남겼다. 폐기 사본 해시는 당시 증거다.
  정상 원본 DLL/Intermediate·사용자 GUI2584와 다른 담당의 예약/입력은 유지했다.

```powershell
& .\Scripts\Tests\Run-ActionProgressTests.ps1 -EngineRoot 'D:/UE_5.8'
```

실행 전 최신 작업 조율 표에 자신의 스크립트/출력 범위를 예약한다. 원본 소스/바이너리 변경이나
전체 게임 빌드는 이 러너의 동작이 아니다. 새 GUI/GPU 화면·실물 입력·실제 전투·다인 네트워크와
상호작용 생산자 연결은 이번 성공 범위에 포함하지 않는다.

작은 증거: `Saved/Tests/ActionProgress/65c2321464dc4387bdfc86fe66b169ce/{Result.json,
Presentation.json,Snapshot.json,AssetHashes.json,NativeCommand.json,ReloadCommand.json}` 및
`Saved/Tests/ActionProgress/46a981cd12834e46952b0e764a04abd4/CleanupFixture.json`.

## 2026-10-31 — 실제 훈련 클라이언트 PIE의 차지 표시 확인

`Run-TrainingPlaytest.ps1`에 `-ObserveActionProgressPIE`와 `-McpPort`를 추가했다.
기본 headless 경로는 유지한다. 선택 시 자기 full Editor의 InViewport PIE를 시작하고,
기존 개발 로그인/HTTP 티켓/Iris 입장·저장된 스킬 편성·훈련 입력 probe를 재사용한다.
새 [관찰기](../Scripts/Tests/ActionProgressPIEObservation.ps1)는 자기 MCP 포트/PID를 확인하고
실제 Slate 트리만 읽는다. UI 값·완료 목표·보상이나 게임 시계를 주입하지 않는다.

설치 DLL `9968DDA8…EA32034`와 플러그인/Config의 독립 사본, Memory TCP3078/
게임 UDP18898/MCP8012에서 최종 실행을 확인했다. 사용자 GUI2584 및 다른 담당의
PIE·소스/공유 빌드·포트는 유지했다.

- 실제 `Charge Slash` 문구와 진행 바가 표시되고 **0.3s / 0단계 → 0.9s / 1단계 →
  1.5s / 3단계·Ready**가 관찰됐다. 입력 해제 뒤 같은 바의 Slate ref와 스킬명·시간/
  Ready가 트리에서 사라졌다. 제품 HUD가 만든 위젯이며 별도 검사 위젯을 띄운 결과가 아니다.
- 기존 서버/클라이언트 목표가 0→1→3→7→15, 실제 훈련 세션이 Cleared가 됐다.
  XP100/레벨2/SP3·해금, 반복 조회와 동료 캐릭터 불변도 기존 러너에서 통과했다.
  PIE 시작/종료 확인과 두 UE 종료 코드 0/0·probe 시작 이후 Error 0/0을 확인했다.
- 이것은 **기존 입력 API를 호출하는 probe**이며 물리 키보드 입력 검사는 아니다.
  차지 한 종류의 실제 PIE/Slate 표시를 확인했다. 홀딩·캐스팅·상호작용 생산자·취소,
  다양한 해상도/색상·가림의 픽셀 비교와 2~4인·Steam/PG는 이번 성공 범위 밖이다.
- 처음에는 MCP 헬퍼의 선택 JSON/세션 상태와 StrictMode 충돌로 PIE 기동 전에 실패했다.
  두 관찰 실행은 진행/회수와 서버 완료를 확인했으나 Ready 표본을 놓쳐 실패로 보존했다.
  game-thread에서 실행되는 깊은 Slate 조회를 초기에는 간격을 두고 완충 근처에는 짧게
  확인하도록 조정했다. 이후 Ready 표시 검사는 통과했지만 종료 실패가 남아, PIE 선택
  경로에만 종료 대기 90초와 역할별 실제 종료/코드 기록을 넣은 뒤 최종 전체 통과했다.
  앞선 일반 종료 오류만으로 정확한 실패 원인을 단정하지 않는다.
- 성공/실패의 자체 RuntimeProject/DLL·플러그인 사본·UserDir/셰이더·백엔드 빌드/캐시를
  정리했다. **계측된 폐기 하한 2,866,771,081 bytes(2.67GiB)**이며 모든 자기 Temp/UserDir가
  없다. 한 회차 DLL 사용 해제 지연으로 일부 정리만 진행된 뒤 중단돼, 원본/다른 프로세스를
  종료하지 않고 해제 후 나머지를 정리했다. 그 부분 삭제량은 하한 집계에서 제외했다.
  작은 결과·서로 다른 UI 단계의 Slate 표본·해시/필수 실패·종료 로그만 남겼다.
  원본 HUD/스킬바·플레이어/차지 관련 에셋 5개는 이전 PIE 시작 전 해시와 동일하다.

최종 전체 실행: `Saved/Tests/TrainingPlaytest/95922e4421e54c5ca348c987e9ff6c39/Verification.proof.json`.
표시와 바 회수의 추출 근거/시도별 결과·정리 집계:
`Saved/Tests/ActionProgress/c8e5c5a6c9724dde936ead177eeb3c54/Completion.json`.
검증 DLL/입력 사본은 폐기했으며 해시는 당시 증거다. 재실행에는 최신 예약 확인과 새 사본이 필요하다.

## 2026-10-31 — 실제 차지 취소와 늦은 입력 해제의 PIE 검증

`Run-TrainingPlaytest.ps1 -ObserveActionProgressPIE -CancelChargePIE`에 차지 취소 시나리오를 추가했다.
선택 모드는 실제 저장된 슬롯의 입력 API로 차지만 시작한다. 기존 기본 모드의 네 동작과
완료/정산 검사는 유지한다. 현재 Development 검사 모듈을 가진 독립 `RuntimeProjectRoot`가 필요하며,
네이티브 취소 probe가 없는 모듈은 실행 전에 거절한다.

새 `RPGActionProgressCancellationPIEProbe.cpp`는 명시적 개발 검사 플래그·nosteam·자기 실행 경로에서만
동작한다. 네이티브 CoreTicker에서 해당 Pawn의 활성 차지에 기존 GAS `CancelAbility`를 호출하고,
같은 실행의 서버 `OnAbilityEnded.bWasCancelled`를 읽는다. 제품 UI/훈련 목표/보상/시계 주입은 없으며
`PlayerUIComponent`에 Tick을 추가하지 않았다. Python은 비활성 상태다.

- 최종 분리 모듈 `57B39807…6FB20821`을 실제 서버/클라이언트가 같은 경로·해시로 로드했다.
  PIE Slate에 `Charge Slash`·**0.4s / 0단계**와 바 `pb5`가 표시됐고, 네이티브 취소 직후
  제품 위젯은 inactive/Cancelled/Collapsed가 됐다. 서버도 Authority 역할에서 Cancelled로 끝났다.
  실제 입력 해제 후 8초까지 같은 취소 상태를 유지했고, 최종 Slate에서 스킬 문구·시간과 같은
  바 ref가 사라졌다. 숨김은 UI 값을 직접 덮어쓴 결과가 아니다.
- 차지만 입력하는 검사이므로 훈련 목표는 서버/클라이언트 모두 **Mask0/안내0/4**다.
  비영 목표 마스크가 한 번도 나타나지 않았고, 실제 세션 InProgress·XP0/Lv1/SP0/revision1과
  전체 성장 JSON·동료 캐릭터가 유지됐다. PIE 시작/종료, 두 UE 종료0/0과 probe 시작 이후
  Error0/0을 확인했다. 기본 모드의 전체 훈련 완료를 이번 결과로 계산하지 않는다.
- 앞선 Python 후보는 `FEditorScriptExecutionGuard` 때문에 Actor RPC가 클라이언트 Role2에서
  로컬 실행되는 오염이 있었다. 엔진 `PyUtil.cpp`/`Actor.cpp`와 실제 취소 관측으로 확인했으며,
  그 결과를 제품의 서버 취소 결함으로 판단했던 설명을 정정한다. ASC/SkillContainer의 임시 진단
  세 파일은 착수 전 해시로 복원했다. 제품 취소 정책을 바꾼 수정은 없다.
- 첫 네이티브 회차는 취소 전 Connect 실패와 에디터 종료 중 크래시였고, 파일 사용 해제 지연으로
  남은 자기 Temp/UserDir는 재개 시 정리했다. 기동 대기 중 같은 서버의 기존 heartbeat를 갱신하고,
  기동/플레이 대기를 나누며 종료 후 정리 재시도를 추가했다. 다음 네 동작 회차는
  `NormalEndNotObserved Action0/Mask0`으로 취소 전에 실패했다. 원인은 확정하지 않았으며 별도
  훈련 입력 검사 후속이다. 최종 차지 전용 성공이 이 기본 공격 실패를 해결했다는 뜻은 아니다.
- 로비 담당이 명시적으로 인계한 `RPGOnlineLobbyWorldTests.cpp`의 프로퍼티 조회만 보완했다.
  최종 NoLink 컴파일과 471개 입력의 분리 링크가 통과했고 제외한 테스트 OBJ는 없다.
  로비 native3/기본 로비 표시 검사는 로비 담당의 후속 범위다. 정상 DLL `9968DDA8…EA32034`는
  교체하지 않았고, 고정 사본 생성 뒤 공용 소스·빌드 예약을 해제했다.
- 이 취소 작업의 성공/실패 임시 DLL/PDB·실행 프로젝트·백엔드 빌드·UserDir/캐시와 중복 로그
  **4,806,828,280 bytes(4.477GiB)**를 폐기했다. 자기 임시 경로/프로세스·최종 정리 예외0이며
  원본 에셋5개는 착수 전 해시와 같다. 작은 결과·해시·필수 실패/종료 로그만 남겼다.

최종 증거: `Saved/Tests/TrainingPlaytest/3f484a97aea743c68a9c0108de1b3b71/Verification.proof.json`,
같은 폴더의 `Cancellation.json`/`ServerCancellation.json`/Slate 표본2개.
시도별 실패와 정리 집계: `Saved/Tests/ActionProgress/e7d08309453748d2a9c6498c75d7a0a4/Completion.json`.
검증 사본은 폐기했으며 해시는 당시 증거다. 물리 키보드·홀딩/캐스팅/상호작용·픽셀 외형·다인/
Steam/PG와 정상 설치 모듈에서의 재실행은 이번 성공 범위 밖이다.

## 2026-10-31 — 홀딩 후속의 실제 경로 확인과 인계 대기

차지 취소 이후 후속을 정하기 전에 최신 조율표와 실제 호출/작성 코드를 다시 확인했다.
현재 `training-completion-install-20261031`이 전체 Source/Config/Plugins 읽기·공용 빌드/정상
Binaries를 예약하고 있으며, 훈련 실행기와 입력 probe도 완료 UI 선택 모드를 위해 수정 중이다.
HUD 후속에서 이 파일들을 동시에 편집하거나 검증 DLL을 복사/재링크하지 않는다.

- **홀딩의 실제 대상은 WhirlWind**다. `ConfigureGreatSwordWhirlwindFinisher.py`가 해당 정의의
  Holding 정책과 루프/End 구간을 검사하며, 마이그레이션 작성기도 Holding config를 구성한다.
  현재 `DA_Skill_WhirlWind`의 직렬화 이름에서 같은 정책/config를 확인했다. 실제 정책은
  `StartExecution`에서 공용 `BeginProgress(Holding, ...)`를 호출한다.
  다음 실행은 이 실제 스킬의 진행/입력 해제/취소와 HUD 회수를 확인하는 범위로 잡는다.
- 현재 `Content/Blueprints/GameData/Skill`의 대검 정의4종을 이름 참조로 검사했을 때 Casting
  정책/config는 없었다. 이것은 **로드된 에셋 속성이나 전체 런타임 카탈로그 검사 결과가 아니다**.
  캐스팅 실플레이 검사는 실제 대상 정의/몽타주가 먼저 필요하며, 이미 있는 것처럼 보고하지 않는다.
- HP/MP 후보인 `RPGPlayerStatusViewModel::SetUIComponent`는 새 구독만 추가하며 이전 구독 해제,
  null 해제, 현재 Attribute 초기 값 읽기를 구현하지 않았다. C++/Plugins 검색에서 외부 호출이
  없고 현재 UI 에셋의 이름 참조 검색도 일치0이었다. 따라서 이를 현재 HUD의 실제 바인딩 결함으로
  단정하거나 사용되지 않는 ViewModel만 먼저 고치지 않는다. HP/MP 추출 전 로드된 WBP의 실제
  바인딩을 확인하고, 이 ViewModel을 연결할 때 초기 스냅샷·재바인딩·해제를 함께 구현한다.

이번 후속은 읽기 대조/인계 기록까지다. 새 Source/스크립트/에셋 적용·후보 재포장·UE/빌드/PIE는
없다. 선행 담당의 고정 입력 해제와 변경된 실행기 인계 후 최신 코드를 재조회해 구현/PIE 범위를
새로 예약한다. 자기 대형 임시 산출물 생성/삭제0 bytes이며 작은 해시/확인 결과만 보존했다.
근거: `Saved/BuildChecks/HudFollowupHandoff-20261031/ReadOnlyAudit.json`.

## 2026-11-01 — HP/MP 전용 위젯 구현·정상 모듈 반영

홀딩 후속의 훈련 실행기가 타 담당의 수정 범위에 있어, 독립된 HP/MP 표시 모듈을 구현했다.
새 `URPGPlayerVitalsWidget`은 GAS의 현재/최대 HP·MP를 읽고 네 Attribute의 변경을 구독한다.
늦게 생성돼도 현재 값을 즉시 표시하며, 최대 값만 바뀌어도 비율/문구를 갱신한다.
Tick/타이머·게임플레이 값 변경이나 새 복제 상태는 추가하지 않았다.

- 기본 모드에서 로컬 소유 Controller의 현재 Pawn을 자동 선택하고, 늦은 possession·Pawn 교체·
  unpossess에 맞춰 소스를 바꾼다. 이전 ASC의 네 구독과 actor EndPlay 구독을 해제하며, 위젯
  Destruct/Destroy·소스 EndPlay/null 지정 때 표시를 지운다. 재구성은 현재 값을 다시 읽는다.
- 명시적 `SetVitalsSource`는 Construct 이전에도 지정할 수 있다. 자동 모드에서는 Construct/
  possession이 소유 Pawn을 선택한다. 별도 소스를 쓰는 미리보기/호스트는 클래스 기본값의
  `bAutoBindOwningPawn=false`를 사용한다. 소스는 등록된 ASC이며 표시 Attribute를 가지고 있어야 한다.
- 작성 WBP의 선택적 `HealthBar`/`ManaBar`는 ProgressBar, `HealthText`/`ManaText`는 TextBlock이다.
  작성된 외형은 유지하고 값만 갱신한다. 작성 트리가 없으면 네 컨트롤의 기본 배치를 제공한다.
  전체 상태는 읽기 전용 `FRPGPlayerVitalsSnapshot`과 표시 변경 이벤트로도 읽을 수 있다.
  0 최대 값·범위 밖/비정상 수치는 유한한 표시 값으로 정리하며 소스가 없으면 모듈을 숨긴다.
- 소스는 신규 `Public/UI/Vitals/RPGPlayerVitalsWidget.h`·`Private/UI/Vitals/RPGPlayerVitalsWidget.cpp`,
  직접 회귀는 `Private/Tests/RPGPlayerVitalsWidgetTests.cpp`다. 경로는 모두 `Source/Project_RPG` 아래다.
  원래 ViewModel·훈련 실행기/검사기·원본 HUD 에셋은 수정하지 않았다. **부모 WBP에 새 모듈을 넣고
  기존 HP/MP 자식/이벤트를 옮기는 작업은 아직 미착수**이며 현재 HUD 외형은 그대로다.

NoLink CPP4/종료0·사설 링크473 입력과 Source1045 불변을 확인하고 사설 UMG/Slate/GAS native4를
실행했다. 이후 훈련 담당의 완료/전체 예약 해제와 UE0을 확인해 정상 타깃을 새로 예약했다.
정상 링크/metadata5작업·종료0으로 설치 DLL **`2B2B45B1…6A28E3898`**에 새 클래스가 포함됐으며,
기존 OptionsUIRuntime 종속 lib/DLL도 링크됐다. 해당 플러그인의 Source 수정은 없다.

같은 설치 DLL을 독립 실행 사본에서 실제 로드해 **native4/4·UE 종료0**을 확인했다.
초기 값/현재·최대 값 변경, 소스/위젯 수명과 오래된 소스 차단, 실제 Controller possession 변경,
0/비정상 수치를 검사했다. 기동 Error15·첫 검사 에셋 경고139와 검사 발견 이후 Error0을 구분한다.
이 결과는 native fixture 범위이며 실제 부모 HUD/PIE·물리 입력 검증은 아니다.

새 `Scripts/Tests/Run-PlayerVitalsTests.ps1`는 기존 UI 사본/안전 정리 도우미를 재사용한다.
모듈 경로를 받아 네 이름의 JSON·성공 완료 로그·실제 로드 경로/해시·종료와 입력 불변을 대조하고,
실패/중단에도 자기 프로세스의 사용 해제 후 사본을 정리한다. 첫 사설 링크 뒤 UTF16만 보던
표식 검사에서 UE 기동 전에 잘못 거절했고, ANSI도 읽도록 보완한 최종 실행만 성공으로 기록했다.

자기 사설 DLL/PDB·실행 프로젝트/설정·플러그인 사본·UserDir/캐시·중복 빌드 로그
**759,768,573 bytes(724.572MiB)**를 폐기했다. 자기 프로세스/Temp·최종 예외0, 원본 HUD2개 해시가 같다.
정상 Binaries/Intermediate는 유지할 빌드 출력이며 검증 사본 해시는 당시 증거다.
본인 Source/스크립트·원본 입력·공용 빌드/정상 Binaries·Content 읽기 예약을 모두 해제했다.

정상 설치 사본 native: `Saved/Tests/ActionProgress/ec410f06c5c642c9be608d229df7c2a3/Result.json`.
구현/빌드·시도·정리 집계: `Saved/BuildChecks/PlayerVitalsWidget-20261101/ImplementationResult.json`.

## 2026-11-01 — 부모 HUD의 HP/MP를 Vitals WBP로 이관

기존 `WBP_PlayerHealthBar`/`WBP_PlayerManaBar` 두 StatusBar 인스턴스를 새
`Modules/WBP_PlayerVitals`로 옮겼다. 새 WBP는 `URPGPlayerVitalsWidget`을 부모로 사용하고,
부모 HUD는 화면 전체에 맞춘 `PlayerVitals` 인스턴스 하나를 배치한다.
HP/MP 내부의 화면 좌표·앵커·자동 크기와 400×40 크기, 머티리얼/브러시·빨강/파랑 색·글꼴은 보존했다.

```text
WBP_PlayerHUD
└─ PlayerVitals : WBP_PlayerVitals
   └─ VitalsCanvas
      ├─ Health_RPGSizeBox_Base → HealthBar / HealthText
      └─ Mana_RPGSizeBox_Base   → ManaBar / ManaText
```

| 편집 대상 | 위치 |
|---|---|
| HP/MP 위치·크기와 내부 외형 | `WBP_PlayerVitals`의 두 SizeBox와 자식 컨트롤 |
| 모듈 전체의 부모 배치/순서 | `WBP_PlayerHUD.PlayerVitals`의 Canvas 슬롯 |
| 현재 값·최대 값·초기 표시/구독 수명 | 기존 `URPGPlayerVitalsWidget` |

바·텍스트 네 컨트롤은 새 모듈의 네이티브 BindWidget 필드에 연결된다. 기존 원시 위젯의
Blueprint 프리셋은 생성 도구가 지원하지 않아, 같은 네이티브 SizeBox/TextBlock에 반영된
외형 속성을 복사했다. 공유 `TPWBP_StatusBar`·프리셋·머티리얼 원본은 저장하지 않았다.

부모의 HP/MP 직접 바인딩/갱신과 미연결 중복 이벤트를 포함한 **37개 노드**를 제거하고
초기화 실행 연결 **1개**를 이어 붙였다. 다른 위젯12개, 나머지 이벤트 노드/핀/연결과
`UpdateValue` 함수는 예상한 실행 연결 변경을 제외하고 대조/보존했다. 외부 참조자2개의
그래프에서 옛 HP/MP 자식 참조가 없고, Scripts/Source의 외부 이름 호출도 없음을 확인했다.
부모 위젯14→13개, 새 모듈13개이며 다른 스킬바·고유 능력·알림/우편 연결은 유지했다.

새 `Scripts/Editor/ExtractPlayerVitals.ps1`는 기본 읽기 전용이고 `-Apply`에서 두 WBP만
저장한다. 자기 소유 MCP Session의 PID/실행 경로/포트를 대조하며 PIE·dirty 원본·예상 밖
구조/다른 기능 침범·외부 파일 변경을 거절한다. 저장 전 다른 위젯/그래프와 입력 해시를 대조한다.
완성된 구조에서 다시 실행한 결과 changed=false·추가 저장0이다.

새 `Scripts/Tests/Verify-PlayerVitalsAsset.py`를 독립 Cmd에서 실행해 디스크 재로드·두 WBP
컴파일/dirty=false/저장0·위치/크기/브러시/색/글꼴 동일을 확인했다. 실제 저장 클래스를 생성해
네이티브 바/텍스트4개와 부모 안의 단일 Vitals 모듈에서도 네 컨트롤의 런타임 바인딩을 확인했다.
**재로드 검사 종료0**이며 이 단계는 에셋/초기화 검사다. 실제 PIE 전투·물리 입력과 다인 HP 복제는
실행하지 않았다. 값 변경/소유 Pawn 수명은 앞 절의 native4 근거를 따르며 이를 이번 새 PIE 성공으로
계산하지 않는다.

첫 미저장 작성은 PS5 인코딩/지원하지 않는 원시 프리셋 클래스/JSON 키 순서 비교를 보완하면서
폐기·재시작했다. 첫 원본 HUD 해시는 그대로였으며, 이후 네 차례 재로드 검사 어댑터의 필드
이름/클래스 노출/비공개 WidgetTree 접근 실패와 최종 성공을 구분해 작은 오류 로그로 남겼다.
최종 읽기는 공개 위젯 목록과 UObject 소유 관계를 사용하며 에셋을 다시 수정하지 않았다.

knockback 담당의 Source/공용 빌드/정상 Binaries 예약을 보존하고, 단기 공유 읽기/변경 감지로
얻은 DLL·설정/플러그인 사본만 실행했다. 원본 DLL 매핑/쓰기·C++/공유 빌드·다른 프로세스 종료0이다.
자기 작성 에디터 두 개는 저장 완료/미저장 폐기 후 자기 PID만 종료했고,
실행 사본/UserDir/캐시·백업과 중복 재로드 로그 **339,075,458 bytes(323.368MiB)**를 폐기했다.
자기 Temp/프로세스·최종 정리 예외0, 사본 해시는 당시 증거다. 본인 에셋/도구/읽기/8022 예약을 해제했다.

결과/시도·정리: `Saved/BuildChecks/HudVitalsModule-20261101/ImplementationResult.json`.
비교 근거: 같은 폴더의 `VitalsBefore.json`/`VitalsPlan.json`/`VitalsApplied.json`/`VitalsRepeat.json`,
최종 재로드: `Reload.json`/`ReloadExit.json`/`Reload.proof.log`.

## 2026-11-01 — Vitals 하단 배치·소유 화면 정리와 PIE 관찰

`WBP_PlayerVitals`의 두 resource SizeBox를 하단 중앙 기준으로 배치했다.
HP는 `(-460,-248)`, MP는 `(244,-248)`, 앵커는 둘 다 `(0.5,1)`이다.
1920×1080 기준에서는 종전 `(500,832)`/`(1204,832)`와 같고, 기존 400×40 크기·스킨은 유지한다.
부모 HUD의 모듈 슬롯은 전체 Canvas에 맞춘 기존 설정을 사용한다.

실제 생성자 `GA_Player_DrawHUD`의 `AddToViewport` 한 호출을 `AddToPlayerScreen`으로 교체했다.
Target·ZOrder·입출력 실행 연결과 다른 노드/핀/값을 대조해 보존했고, 새 bool 반환 핀은 미연결이다.
이는 소유 플레이어의 화면에 등록하는 변경이며, 아래 비표시 PIE의 0크기 문제를 해결했다고 판정하지 않는다.

- `Scripts/Editor/AnchorPlayerVitals.py`: 기본 읽기 전용, `-RPGAnchorPlayerVitals`에서 모듈 하나만 저장.
  두 Canvas 슬롯 외 위젯13개의 속성/계층과 부모 HUD를 대조한다.
- `Scripts/Editor/ScopePlayerHUDToLocalPlayer.py`: 기본 읽기 전용, `-RPGScopePlayerHUD`에서 생성자 하나만 저장.
  함수 한 개 교체에 따른 노드 경로와 미연결 반환 핀 이외의 전체 그래프를 대조한다.
- 두 변경 모두 Cmd 작성/새 재로드·컴파일이 종료0이다. 모듈 재로드는 native4 바인딩도 통과했다.
  모듈/생성자 반복 검사에서 changed=false·추가 저장0을 확인했다.
- 최종 모듈 해시 `A3680E2E…B2D652D6`, 생성자 `007AA2C3…244F482`,
  부모 HUD `038071F7…1A150341`이다. 부모 HUD는 앞 이관 단계의 해시와 같다.

`Run-TrainingPlaytest.ps1 -ObservePlayerVitalsPIE`와 새
`PlayerVitalsPIEObservation.ps1`/`Observe-PlayerVitalsPIE.py`는 실제 훈련 입장 후 저장된 부모 HUD 안의
단일 모듈을 읽는다. Pawn의 실제 AttributeSet, native snapshot, 바 percent와 TextBlock을 대조한다.
GAS/UI 값·스냅샷·입력 시계는 설정하지 않는다. Slate 콜백은 검사 프로세스에만 있고 제품 위젯의
Tick/타이머는 추가하지 않았다. 이 옵션의 최종 범위는 **초기 Vitals 표시**이며,
기존 입력 probe가 실행될 수 있어도 훈련 네 동작/완료·보상 성공으로 계산하지 않는다.

비표시 full Editor PIE에서 소유 클라이언트 `BP_TrainingPlayer`/AutonomousProxy와 실제 저장 모듈의
연결을 확인했다. HP `200/200`, MP `100/100`, 두 percent `1`이 실제 GAS 값과 같았다.
그러나 바와 화면 host의 cached geometry가 계속 0으로 읽혀 **실제 그려짐 검사는 미통과**다.
하단 앵커/플레이어 레이어 변경 뒤에도 이 판정은 유지한다. 작은 에디터 창·가려진 레벨 탭·비표시
렌더링/배치 경로는 검사 중 가설이며, 확인된 제품 결함 원인으로 단정하지 않는다.

초기 Python 함수/속성 노출 오류, JSON 파일 교체와 Windows 읽기 핸들 충돌, 기동/PIE HTTP 시간 초과,
0크기 판정은 최종 성공과 구분해 남겼다. Windows 결과 파일 읽기는 delete sharing을 허용하고,
에디터/서버 병행 기동과 공개 PIE 제어를 사용한다. 최신 모듈의 별도 전체 훈련 시도에서는
`NormalEndNotObserved Action=0`도 관측했다(`dd2dd6e6aba04a439d073f53be06a086/client.proof.log`).
UI 초기 표시 검사로 이 일반 공격/훈련 실패를 해결·통과했다고 기록하지 않는다.
실제 피해/마나 소비에 따른 변화·물리 입력·다인 복제도 이번 근거 밖이다.

제품 C++/공유 빌드·원본 DLL 쓰기 없이 각 시도의 닫힌 DLL/설정/플러그인·HUD overlay를 사용했다.
시도 간 정상 모듈은 `3761…`/`7CD0…`/`4608…`로 달라 각 근거의 해시를 따르며, 폐기한 사본을
동일 입력으로 다시 실행할 수 있다고 보지 않는다. 이 회차의 18개 독립 실행 사본/15개 훈련 시도의
임시 DLL·캐시·UserDir·백업/전체 로그 **2,435,892,866 bytes(2.269GiB)**를 정리했다.
현재 자기 UE 프로세스·Temp/TP 잔여0이고, 원본 Binaries/Intermediate·공유 DDC는 정리 대상으로 삼지 않았다.
작은 명령/해시·시도별 최소 로그와 작성 결과만 보존한다.

작성/재로드: `Saved/BuildChecks/PlayerVitalsPIE-20261101/{AnchorResult,AnchoredReload,RepeatResult,HostResult,HostReloadResult}.json`.
시도/정리 집계: 같은 폴더 `ProgressResult.json`.
이 회차 종료 시에는 보이는 전용 PIE 창의 실행을 사용자에게 질의한 상태였고 실행하지 않았다.
그 이후 표시 실행과 최종 결과는 다음 절에 기록한다.
`-ShowVitalsEditor`는 해당 전용 클라이언트 창에만 적용하고 종료/실패 시 자기 프로세스와 임시 입력을 회수한다.

## 2026-11-01 — 표시 PIE 검증 완료·자원 바 기본 시간 문구 정리

후속 `다음작업` 지시에 따라 전용 표시 에디터/PIE에서 재확인했다. Python이 읽은 cached geometry는
계속 0이었지만, **같은 실행의 native Slate 트리에는 HP/MP 바와 텍스트의 유효한 배치가 존재**했다.
따라서 앞의 0크기 기록만으로 제품이 그려지지 않는다고 판단할 수 없다. 앵커나 화면 등록 변경이
이 0값의 원인을 해결했다는 뜻도 아니다. Python 경유 geometry 값이 달라지는 엔진 내부 원인은 미확정이다.

관찰기의 배치 판정은 실제 소유 에디터의 단일 PIE 창으로 제한한 native Slate 기준으로 바꿨다.
각 resource의 정확한 텍스트와 직전 progress bar를 대응시키고, 양수 크기·창 내부 배치·텍스트 중심과
바의 대응·서로 다른 bar ref를 확인한다. 에디터의 같은 문구나 다른 PIE 창은 성공 근거로 쓰지 않는다.
Python은 실제 Pawn/AttributeSet·native snapshot·바 percent/TextBlock의 값 대조를 계속 담당한다.
`Test-PlayerVitalsSlate.ps1`의 14사례에서 정상/동일 HP·MP 문구 및 실제 캡처를 수용하고,
0크기·NaN·누락/중복 문구·다중 PIE·창 밖/다른 영역·같은 bar 오인식은 거절했다.
이는 과거 캡처를 파서 입력으로 확인한 것이며 폐기한 옛 DLL을 다시 실행한 검사가 아니다.

첫 표시 실행의 캡처에서 resource 바에 불필요한 템플릿 `0.1s` 두 문구를 발견했다.
새 `Scripts/Editor/ConfigurePlayerVitalsSkin.py`는 기본 읽기 전용이며,
`-RPGConfigurePlayerVitalsSkin`에서 `WBP_PlayerVitals`의 `Health_ProgressTimeText`/
`Mana_ProgressTimeText` visibility 두 개만 Collapsed로 저장한다. 기존 13위젯의 계층·바/텍스트4개·
배치·스킨 속성과 부모 HUD/생성자 해시를 대조한다. Cmd 저장/새 재로드·native binding/무변경 반복은
세 단계 모두 종료0, 추가 저장0이다. 최종 모듈 해시는 `EF5A47CB…D2B35EC7`이고 부모 HUD/생성자는 그대로다.

최종 실제 HTTP 입장/Iris 소유 클라이언트의 표시 PIE에서는 HP `200/200`, MP `100/100`, percent1이
GAS와 같았고, native Slate의 두 바 배치(보고 크기390×30)와 문구 대응을 확인했다.
두 timer가 실제 인스턴스에서도 Collapsed임을 읽고, 같은 창 PNG를 확인해 `0.1s`가 사라진 것을 확인했다.
PIE 시작/종료·서버/클라이언트 종료 **0/0**, probe 시작 이후 예상 밖 오류 **0/0**이다.
이는 초기 표시 검증이다. 실제 피해/마나 소비에 따른 변화·물리 입력·다인 복제·훈련 네 동작/보상
완료는 이번 성공에 포함하지 않는다. 해당 미검증 항목의 보고 문구만 실행 후 정정했으며 결과 재실행은 없다.

현재 정상 `552DB6D7…0C421F19`의 사본을 사용했고 게임 C++/공유 빌드·원본 DLL 쓰기는 없다.
이번 후속의 사본4회/훈련 실행3회에서 임시 DLL/백업/캐시/UserDir·중복 캡처
**556,617,809 bytes(530.832MiB)**를 폐기했다. 앞 회차 합계는 **2,992,510,675 bytes**이며,
자기 UE/Temp/TP 잔여·정리 예외0이다. 최종 전달용 PNG 하나(786,245 bytes)와 작은 결과/해시/필요 로그만
남겼고, 이전 중간 PNG는 해시/문구 근거만 남겨 폐기했다. 사본 해시는 당시 실행 증거다.
본인 에셋/스크립트/문서·읽기/포트/실행 예약을 해제했다.

최종 결과/정리: `Saved/BuildChecks/PlayerVitalsPIE-20261101/CompletionResult.json`.
실제 PIE: `Saved/Tests/TrainingPlaytest/0378b226d6794080801edebe99365921/Verification.proof.json`.
같은 폴더 `Vitals.json`/`VitalsLayout.json`/`vitals-slate.txt`와 최종 `vitals-window.png`.

## 2026-11-02 — 튜토리얼 HUD Ability의 생성 책임 이관 완료

실제 `GA_Player_DrawHUD`는 ActivateAbility → WBP_PlayerHUD 생성 → AddToPlayerScreen → EndAbility의
5노드 래퍼였다. 생성 노드의 OwningPlayer는 비어 있었고, AssetRegistry의 참조자는
`DA_Player.ActivateOnGivenAbilities` 한 곳이었다. 기존 HUD 설정의 참조자는 기본/보스/서바이벌/훈련
GameMode 네 곳이다. 이 참조 조사만으로 미실행 경로까지 검증했다고 보지는 않는다.

새 `ARPGHUD.PlayerHUDWidgetClass`는 기존 BP_RPGHUD에서 외형 클래스를 지정하는 경계다.
HUD가 명시적인 로컬 소유 PlayerController로 위젯을 만들고 AddToPlayerScreen에 등록한다.
Pawn이 없으면 만들지 않고, 빙의 변경 이벤트에서 시각 트리를 교체한다. 이는 기존 Blueprint 모듈들이
OnInitialized에서 Pawn/UIComponent를 한 번 잡는 수명을 보존하기 위한 처리다. 같은 Pawn의 중복 이벤트는
현재 인스턴스를 유지하며, 빙의 해제/EndPlay에서는 이전 화면과 소유 참조를 비운다.
Tick/타이머/새 GameplayAbility는 추가하지 않는다. 초기화/Construct/Destruct 콜백 중 재빙의나 종료가
일어나면 revision과 실제 현재 Pawn을 재확인해 뒤늦은 위젯이 화면에 등록되지 않게 한다.

`Scripts/Editor/MigratePlayerHUDOwnership.py`는 기본 읽기 전용이다. 명시적 이관 시 HUD의 클래스 설정과
시작 지급 목록의 DrawHUD 항목만 변경하고, 다른 HUD 기본값/게임플레이 능력/효과/외형 WBP 해시를 대조한다.
지급 목록 저장 뒤 참조가 0인 것을 확인해야 옛 Ability를 제거한다. 저장 후 새 프로세스의 재로드도 필요하다.
`BP_RPGHUD.PlayerHUDWidgetClass`를 기존 WBP로 지정하고 `DA_Player`에서 DrawHUD 지급 하나를 제거했다.
남은 참조0을 확인한 뒤 `GA_Player_DrawHUD.uasset`를 삭제했다. 초기 읽기 조사와 최종 저장 기본값을
직접 비교해 다른 시작 능력/효과와 기존 HUD 설정이 모두 같음을 확인했다. 외형 WBP 해시도
`038071F7…1A150341`로 보존됐다.

정상 모듈 `06E2BA60…9A6C892B`에 Source4가 일치하는 해시로 포함됐고, 같은 모듈의 닫힌 사본에서
native8(새 수명2/진행 HUD2/Vitals4)이 모두 Success/오류0이다. 기존 배치 검사의 에셋 로딩 경고139는
남아 있으며 나머지7개는 경고0이다. 새 Cmd 재로드도 같은 모듈에서 HUD/외형 Blueprint 컴파일,
추가 저장0/changed=false, 옛 Ability 파일/참조0을 확인했다.

이어진 타 담당의 정상 디버프 코드 빌드가 `983E24AD…26EF6446`을 만들었다. 이번 HUD Source4는
변하지 않았고, 실제 표시 PIE는 이 새 정상 모듈의 닫힌 사본으로 실행했다. native8 전체를 983E에서
반복한 것은 아니다. 실제 HTTP 입장/Iris 소유 클라이언트에서 표시된 단일 WBP가 native HUD getter와
동일한 객체이며 소유 PlayerController가 맞음을 확인했다. HP200/200·MP100/100과 percent1이 GAS와
일치했고 native Slate의 두 바/텍스트 배치와 같은 창 PNG도 확인했다. 시작/종료·서버/클라이언트 종료0/0,
probe 이후 예상 밖 오류0/0이다. 실제 저장 WBP의 네트워크 재빙의/피해·마나 소비/다인 검사는 이번에
추가하지 않았다. C++ 수명 검사의 Pawn 교체와 실제 PIE 초기 표시를 구분한다.

이관 도구의 첫 Cmd는 두 의도한 설정을 저장한 뒤 원본 OS 파일명을 private /Game 마운트에 재검색해서
중단됐다. package 이름으로 수정한 다음 옛 Ability 삭제는 성공했으나, 사라진 패키지의 참조 조회가
None을 반환해 결과 기록에서 두 번째로 실패했다. 빈 목록 처리를 보완한 최종 새 프로세스 재로드와
위 초기/최종 기본값 비교가 통과했다. 두 실패를 게임 소스 성공이나 완전한 첫 시도 성공으로 소급하지 않는다.

다른 소스 예약을 확인해 중단한 본인 공유/독립 빌드 기록도 보존했다. HLO의 PCH/OBJ/소스 사본과
자기 Cmd/PIE runtime·DLL·UserDir·캐시·백업/덤프·중복 로그 합계 **4,016,447,196 bytes(3.741GiB)**를
자동 폐기했다. 자기 임시/프로세스 잔여·정리 예외0이며 원본 Binaries/Intermediate·공유 DDC는 유지했다.
작은 명령/해시/필요 로그와 최종 전달용 PNG 하나(813,110 bytes)만 남겼다. 폐기한 모듈의 해시는 당시 증거다.
본인 Source4/에셋3/도구2/문서·읽기/빌드/포트/임시 예약을 해제한다.

결과/정리: `Saved/BuildChecks/HudOwnerLifecycle-20261102/CompletionResult.json`.
저장 비교/재로드: 같은 폴더 `MigrationComparison.json`, `ReloadResult.json`.
실제 PIE: `Saved/Tests/TrainingPlaytest/69d83302e16b465e980a6f070b0588a3/Verification.proof.json` 및 `Vitals.json`.

## 2026-11-02 — 남은 표시 모듈과 상호작용 생산자 이관 완료

`WBP_PlayerHUD`는 모듈의 배치를 소유한다. 알림의 상태/수명은 native 위젯으로,
아이덴티티의 기존 게이지/아이콘/활성 애니메이션은 독립 WBP로 옮겼다.
부모 EventGraph에는 우편 버튼을 여는 4노드만 남았다.

| 책임 | 편집 위치 |
|---|---|
| 모듈 배치와 우편 버튼 | `WBP_PlayerHUD` |
| 알림 외형 | `Modules/WBP_PlayerNotice` |
| 알림 교체·3초 수명·Pawn/UI 구독 해제 | `URPGPlayerNoticeWidget` |
| 기존 아이덴티티 게이지·아이콘·InterpUp | `Modules/WBP_PlayerIdentity` |
| 상호작용 진행 데이터와 실행 핸들 | `URPGInteractionProgressComponent` |
| 상호작용 완료 타이머·대상 호출 | 기존 `ARPGPlayerController` |
| Holding/Casting/Charge/Interaction 공통 표시 | 기존 `PlayerUIComponent`와 진행 표시 모듈 |

알림은 `OnNoticeTextChanged`를 구독한다. 새 문구가 이전 문구와 만료 타이머를 대체하며,
빈 문구·잘못된 수명·소스 종료·빙의 변경·Destruct 때 비운다. 만료 revision으로 이전 타이머가
새 문구를 지우지 못하게 했다. 외형/배치는 보존하고 초기 `Notice` 문구는 숨겼다.
부모의 Assign/SetText/Delay/Visibility 체인과 중복 알림 이벤트를 제거했다.

아이덴티티는 기존 Rage 이벤트 채널과 19개 연결 노드, 두 표시 위젯의 속성/배치,
`InterpUp` 애니메이션의 이름·구간·바인딩을 보존했다. 클래스별 게이지 규칙이나 새 폭주 수치를
이 추출에서 바꾸지는 않았다. 전체 화면 모듈/루트는 SelfHitTestInvisible로 설정했다.
부모의 미연결 Construct/Tick/중복 이벤트와 참조 없는 UpdateValue도 제거했다.
직접 참조자의 함수 호출과 Designer binding을 확인했으며, 계속 참조되는 공용 템플릿은 유지했다.

상호작용은 기존 Controller의 Begin/완료/취소 경로에서 공통 진행 API를 호출한다.
새 컴포넌트에는 Tick이 없고 기존 Controller Tick에서 활성 실행만 갱신한다.
PlayerUIComponent에 Tick을 추가하지 않았다. 대상 소멸·Pawn 변경·컴포넌트 해제 때 자기 핸들만
종료하며, 새 스킬 표시로 대체되면 이전 상호작용의 갱신/종료가 새 표시를 지우지 못한다.
public End API는 SourceLost를 허용하지 않으므로 생산자는 수명 종료를 Interrupted로 변환한다.
기존 상호작용 위젯의 항상 0을 반환하는 Percent 바인딩은 해제하고 해당 바를 숨겼다.
참조가 완전히 조사되지 않은 옛 C++ 호환 getter는 유지했다.

`ExtractPlayerHUDModules.py`와 `ExtractPlayerHUDIdentity.py`는 기본 읽기 전용이며,
각각 명시적 적용 플래그에서만 저장한다. 소유 예약/dirty package/참조와 관련 없는 위젯 속성까지
대조한다. Editor 전용 `URPGHUDModuleTools`는 애니메이션 복사본과 바인딩을 검증한 뒤,
엔진의 애니메이션 삭제 절차처럼 원본 객체와 변수 GUID를 함께 정리한다.
최종 새 Cmd 재로드·Blueprint 컴파일·추가 저장0을 확인했다.

빌드에서는 추적 상태가 바뀐 기존 테스트들이 Unity 파일 하나로 합쳐져 익명 namespace 이름이
충돌하는 문제를 발견했다. 게임 모듈의 `bUseUnity=false`로 번역 단위 경계를 고정했다.
설치 UE5.8의 CLI DisableUnity만으로는 해결되지 않았으며, 다른 테스트를 수정하지 않고
정상 전체 682작업과 후속 증분 빌드가 통과했다.

검증은 다음 범위에서 끝났다.

- native14: 새 Notice3/Interaction3와 기존 HUD2/ProgressHUD2/Vitals4 모두 Success, 오류0.
  기존 배치 검사에서 에셋 로딩 경고139는 남는다. 모듈 `445472C5…ABA3CA5`의 닫힌 사본 결과다.
- 최종 실제 HTTP 입장/Iris 소유 클라이언트 PIE: 서버 GameplayEffect로 HP200→175/MP100→90,
  실제 GAS/텍스트/바 값 일치와 생산 알림 이벤트를 확인했다. Holding/Casting API 및 Interaction
  생산자의 데이터가 실제 UMG의 종류/문구/바/종료 숨김에 반영되고 늦은 핸들 종료가 거절됐다.
- 서버 Pawn 교체/재빙의 뒤 새 HUD와 200/100 초기값, 이전 HUD의 화면 제거, 알림 초기화를 확인했다.
  마지막 화면의 Slate 배치와 PNG도 확인했다. 서버/클라이언트 종료0/0·예상 밖 오류0/0이다.
  PIE 모듈 `F84EF1C1…D5A7483B`는 위 native 이후 opt-in 검증 probe만 보완한 빌드다.
  제품 소스와 에셋을 바꿔 native 성공을 재사용한 것은 아니다.
- 마지막 러너의 finally 결과 읽기만 보완한 뒤, 결과 누락/깨진 JSON/정상 결과3사례에서 기존 실패를
  보존하고 정리 단계로 계속 진행함을 확인했다. 게임 검증은 반복하지 않았다.

Holding/Casting은 **source API를 통한 표시 자극**이며 실제 해당 GAS 스킬 발동을 뜻하지 않는다.
Interaction도 생산자 API 표시 검사이며 물리 입력/완료 타이머의 게임플레이 E2E는 아니다.
실제 피격/스킬 비용 실행, 제품 사망/부활, 다인/Steam/PostgreSQL은 이 성공 범위에 포함하지 않는다.
첫 PIE의 이전 Pawn 즉시 Destroy로 발생한 in-flight Iris 오류2는 실패로 보존했다.
최종 probe는 이전 Pawn을 숨기고 월드 종료까지 유지해 재빙의 표시 수명만 검사했다.
첫 추출/fixture/빌드 실패도 최종 성공으로 소급하지 않는다.

자기 runtime/DLL/UserDir/캐시·중복 작성본/중간 PNG를 **최소 1,233,838,964 bytes(1.149GiB)**
폐기했다. 첫 잠금 정리의 부분 삭제량이 측정되지 않아 합계는 하한이다.
자기 Temp/실행 잔여·정리 예외0, 원본 Binaries/Intermediate·공유 DDC·타 담당 산출물은 유지했다.
최종 PNG 하나(856,675 bytes)와 작은 명령/해시/필요 로그만 보존했다.
폐기한 모듈 해시는 당시 증거로만 취급한다.

기능에 필요한 native 회귀 검사와 명시적 opt-in PIE 경로는 기능 변경에 함께 유지한다.
검증 결과/로그/캡처/Saved의 임시 실행기는 커밋 대상에서 제외한다.
결과: `Saved/BuildChecks/HudRemaining-20261102/CompletionResult.json`.
실제 PIE: `Saved/Tests/TrainingPlaytest/e5b74944018b47a5b37aea69f938a0e9/Verification.proof.json`.

현재 요청한 잔여 HUD 모듈화/상호작용 연결/표시 검증 범위는 완료했다.
새 게임플레이 규칙이나 추가 플레이 시나리오는 해당 담당/사용자 범위를 확인한 뒤 진행한다.

## 2026-11-02 — 실제 WhirlWind 홀딩 검증과 외부 취소 사유 수정

실제 저장된 `DA_Skill_WhirlWind`는 Holding 정책을 사용한다. `AM_PlayerToggleSkill`은
재사용한 몽타주 이름이다. 이전 문서에서 Toggle로 부른 부분을 실제 실행 정책으로 해석하면 안 된다.
소유 클라이언트의 EnhancedInput 액션 → 기존 퀵슬롯 → 실제 GAS 능력 → 공통 진행 표시를 검사했다.

이 실행에서 몽타주 중단으로 홀딩을 취소해도 표시의 종료 사유가 Completed로 남는 결함을 발견했다.
`URPGSkillExecutionPolicy_Holding::CancelExecution`이 resolved만 설정하고 취소 플래그를 누락해,
공통 정리가 정상 완료로 분류했다. 정리 전에 `bFinishAsCancelled=true`를 설정하도록 수정했다.
이미 종료한 핸들은 다시 쓰지 않으므로 정상 완료 뒤의 늦은 정리와 새 실행은 그대로 보호한다.

직접 회귀 `HoldingCancellationReason`을 추가했다. 외부 취소·늦은 입력 해제·이전 실행의 반복 취소와
새 표시 보존·정상 해제 완료·이미 완료한 표시의 사유 보존을 확인한다. 정상7작업/종료0,
Source/Plugins 입력1382 불변, 동일 `8608EBE5…D73AAC7` 모듈의 닫힌 사본에서
SkillActionProgress native5 모두 Success/오류·경고0이다.

같은 모듈의 최종 실제 HTTP 입장/Iris 소유 클라이언트 PIE 결과는 다음과 같다.

| 실제 입력 경로 | 실제 진행 | 종료 표시 |
|---|---|---|
| 약0.7초 뒤 입력 해제 | Holding 문구/바/몽타주가 여러 프레임 진행 | 작성된 성공 구간에 따라 Completed·숨김 |
| 4초 끝까지 유지 | 약3.95초까지 진행 표본39개·이후 정상 종료 | Completed·숨김 |
| 약0.7초 뒤 몽타주 중단 | 실제 AnimInstance 중단과 이후 입력 해제 | Cancelled·숨김 |

UI 상태나 진행 API에 값을 직접 넣지 않았다. 입력은 EnhancedInput API로 주입했으며 물리 키보드,
실제 피격 경직, Casting 스킬 발동, 다인/Steam/PostgreSQL은 이 성공 범위에 포함하지 않는다.
훈련 해금은 별도 Memory 백엔드 fixture에서 기존 정산 API로 마련한 전제이며 자연 훈련 완료의 증거는 아니다.
서버/클라이언트 종료0/0·probe 이후 예상 밖 오류0/0, 마지막 Slate 배치와 PNG를 확인했다.

첫 세 실행의 조회429/내부 Python 라이브러리 접근/PC 시간에 따른 부족한 진행 표본은 실패로 보존했다.
그 다음 입력/숨김 검사는 통과했지만 취소 사유 결함이 남았던 결과와, 위 수정 후 최종 성공을 구분한다.
검증 전용 실행기/관찰기/결과는 Saved에만 두고 직접 회귀와 제품 수정·계약만 기능 커밋에 포함한다.
자기 runtime/DLL/UserDir/캐시·중간 PNG **1,048,925,777 bytes(1000.334MiB)**를 자동 폐기했다.
Temp/프로세스 잔여·정리 예외0이며 원본 Binaries/Intermediate·공유 DDC·타 담당 산출물은 유지했다.
최종 PNG1과 작은 명령/해시/필요 로그만 보존했고, 폐기한 모듈 해시는 당시 증거로만 취급한다.

결과: `Saved/BuildChecks/HudHoldingPIE-20261102/CompletionResult.json`.
실제 PIE: `Saved/Tests/TrainingPlaytest/f64903b2c600440bb97ee4d3623383fe/Verification.proof.json`.

## 2026-11-03 — 상호작용 입력의 시작·해제 연결 수정

실제 `DA_InputConfig`의 `InputTag.PickUp.Items`가 기존 자동 바인딩에서
`Input_PickUp.Items`로 변환돼 native 함수 `Input_PickUp_Items`를 찾지 못했다.
또한 Triggered만 연결해 버튼 해제/입력 취소가 Controller의 EndInteract에 도달하지 않았다.

자동 함수 이름은 태그의 하위 구분점도 `_`로 변환한다. PickUp 입력은 Started에서 한 번 시작하고
Completed/Canceled에서 종료하도록 분리했다. 이 경로는 raw action value 대신 명시적 true/false를
전달해 엔진의 Started/Completed 값 반환 설정에 영향을 받지 않는다. 다른 이동 입력의 연속 Triggered,
스킬/아이템 슬롯 및 창 입력의 기존 바인딩 정책은 유지했다. 새 Tick/게임플레이 타이머는 추가하지 않았다.

`ReflectionAndReleaseRouting` 회귀는 실제 `ARPGPlayerController`와 자동 생성된 입력 바인딩을 사용한다.
중첩 태그의 3개 바인딩, 빈 Started 값의 true 전달, 반복 Triggered 없음, Completed/Canceled가
기존 Controller를 통해 공용 표시를 Cancelled로 종료함을 확인한다. 기존 producer3과 함께
native4 Success/오류0이다. PC 기본 에셋 로딩 경고139는 기존 경고로 구분한다.
첫 검사 월드는 초기화되지 않아 Actor::ProcessEvent가 호출을 거절했으며,
InitializeActorsForPlay를 추가한 뒤 통과했다. 그 첫 실패를 제품 입력 실패나 최종 성공으로 소급하지 않는다.

제품 Source3은 정상 증분/입력1389 불변의 `CE72AF5D…07942C8`에서 확인했다.
후속 `619D6E9F…95AB8BA` 모듈도 별도의 닫힌 사본에서 native4를 통과했다.
그 사이 다른 담당자의 새 코드가 포함된 전체 빌드 실패와 실행 중 입력 변경은 별도 기록으로 보존한다.
이번 작업에서 해당 코드를 수정하거나 전체 프로젝트 빌드가 통과했다고 표시하지 않는다.

직접 필요한 `RPGInteractionInputTestTarget`은 Editor 모듈에만 둔 transient 검사 대상이다.
명시적 `RPGInteractionInputPIE`에서만 2초 상호작용을 구성하고 충돌을 켜며 시작/취소/완료 호출을 센다.
게임 배포에는 포함하지 않고, 실제 아이템 지급이나 서버 pickup RPC를 호출하지 않는다.
Editor 검사 객체의 컴파일은 완료했으며, 공유 import LIB가 없는 상태에서는 닫힌 game DLL의
export로 검사 전용 import LIB를 만들고 별도 Editor DLL에 링크했다. 원본 DLL은 쓰지 않았다.

최종 PIE `794cea33275e44a1812da9acd4276fe7`는 같은 `619D6E9F…95AB8BA` game DLL과
별도 Editor 검사 DLL `4F1294E9…4291ED9`를 실제 로드했다. 작성된 IA_PickUp의 EnhancedInput 주입에서
Controller의 주변 대상 탐색·기존 완료 타이머·interface 호출·공용 UMG까지 연결해 다음을 확인했다.

- 약 0.53초 입력 해제: Cancelled/진행 바 숨김, 대상 호출 누적 시작1·취소1·완료0.
- 2초 유지 완료: Completed/진행 바 숨김, 누적 시작2·취소1·완료1.
  완료 뒤 0.5초 더 누르고 있어도 재시작/추가 완료 호출이 없다.
- 다음 시작 뒤 대상 소멸: Interrupted/진행 바 숨김, 소멸 전 누적 시작3·취소1·완료1.

서버/소유자 종료0/0·예상 밖 및 probe 이후 오류0/0이다. PIE에서는 표시 상태를 직접 주입하지 않았다.
물리 키보드, 실제 아이템의 서버 pickup RPC/지급, 2~4인, Steam/PostgreSQL은 검증 범위에 포함하지 않는다.
기동 SDK 조회의 자기 Build.bat 대기 트리만 해제한 검사 환경이므로 패키징 SDK 검증도 아니다.

자기 runtime/OBJ·LIB/DLL·UserDir·캐시 2,479,299,700 bytes(2364.444MiB)를 폐기했고
자기 Temp/프로세스 잔여0이다. 마지막 임시 export 목록 `CurrentGame.def` 905,227 bytes는
자동 승인 검토가 삭제를 정책상 차단해 `Saved/BuildChecks/HudInteractionInput-20261102/`에
정리 예외로 남겼다. 원본 Binaries/Intermediate·공유 DDC·다른 담당 변경은 보존한다.
스냅샷 폐기 후의 해시는 당시 실행 근거이며 같은 바이너리를 재실행할 수 있다는 뜻이 아니다.

검증 전용 실행기/관찰기/명령/로그/최종 PNG는 Saved의 로컬 근거로만 남긴다.
직접 필요한 유지용 회귀와 Editor 검사 대상은 이 입력 결함의 기능 커밋에 함께 포함한다.
결과: `Saved/BuildChecks/HudInteractionInput-20261102/CompletionResult.json`.
실제 PIE: `Saved/Tests/TrainingPlaytest/794cea33275e44a1812da9acd4276fe7/Verification.proof.json`.
