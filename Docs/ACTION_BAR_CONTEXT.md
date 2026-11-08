# 상태별 공통 액션바 — 2026-11-06

현재 단계: 공통 전환/입력/HUD·사용 불가 안내와 실제 Riding 승인 상태를 읽는 로컬 UI 어댑터/하차 연결·중앙 탈것 정보까지 구현·검증 완료(2026-11-08). 실제 Riding 권한/이동/액션 실행은 탈것 담당 범위다.

## 연결 계약

- `UQuickSlotComponent::GetActionBarContext()`는 현재 Pawn에 귀속되는 공통 전환기다.
- `IRPGActionBarProvider`는 C++ 어댑터다. `GetActionBarPawn`, `GetActionBarProfile`, `ReadAction`, `PressAction`, `ReleaseAction`을 구현한다. Pawn/feature component는 weak pointer로 참조한다.
- `URPGActionBarProfile` Data Asset에는 ProfileId, Actions(최대 8개), LeftWidgetClass, CenterWidgetClass가 있다. 각 액션은 안정 ActionId, Label/Icon과 선택적 SkillSlot/InputTag를 가진다.
- native 배치는 액션의 `DisplaySlot`(0~7, -1은 자동)과 프로필의 `NativeSlotCount`(0은 자동, 고정1~8)로 정한다. 명시한 위치를 먼저 예약하고 나머지 액션을 배열 순서대로 빈 위치에 채운다. 고정 칸 수의 빈 위치는 클릭/키/제공자 호출이 없는 자리 표시다. 중복 위치·범위 초과·고정 칸 부족은 활성화 전에 거절한다. 기본값은 기존의 조밀한 순서 배치와 같다.
- 서버 확정 상태를 수신한 feature가 `Context->Activate(Provider)`를 호출한다. 같은 provider/profile은 멱등. 교체는 먼저 기존 입력을 해제한다.
- 하차/강제 종료 때 `Context->Deactivate(Provider)`. 다른 provider로 교체된 뒤 늦은 이전 해제는 무시한다. Pawn 종료는 공통 Shutdown.
- 상태를 중복 복제하지 않는다. 공통 전환기는 승인을 발급하거나 RPC를 보내지 않는다. Riding의 서버 상태/복제/리비전 가드가 유효할 때만 설치한다.
- Riding은 자신의 서버 전투/아이템 사용 가드를 계속 담당한다. HUD나 로컬 입력 차단을 서버 보안으로 취급하지 않는다.
- 현재는 기본 직업 UI 위에 하나의 제공자를 활성화하는 구조다. 제공자 교체는 지원하며, 우선순위 스택이나 여러 상태의 동시 합성은 포함하지 않는다.
- 활성 프로필 정의는 불변으로 취급한다. 쿨타임/사용 가능 여부는 `ReadAction`에서 갱신하고, 액션 목록/위젯 구성이 바뀌면 새 프로필/제공자로 교체한다.
- 중앙 표시가 필요한 제공자는 선택적 `ReadIdentity()`로 `FRPGActionBarIdentityView`(bVisible/Title/Status/Detail)를 반환한다. 기본 구현은 빈 view다. context는 현재 소유 Pawn과 읽기 전후 revision/provider·로컬 제어/Controller를 확인하고, 위젯의 `ReadIdentity()`는 자신의 바인딩 revision도 확인한다. 프로필 교체/종료 중의 오래된 중앙 위젯은 새 제공자의 정보를 읽을 수 없다.

## 입력과 표시

- 기존 스킬 슬롯 키는 현재 프로필의 SkillSlot으로 전달된다. 기존 능력 입력 태그는 InputTag로 전달되며 프로필에 없는 전투/아이덴티티 입력은 override 중 소비된다.
- Riding 전용 IMC/추가 키 제공자는 `PressAction(Source, ActionId, Context->GetRevision())` / `ReleaseInput(Source)`로 단일 경로에 합류한다. Source는 물리 입력별로 고유해야 한다. 동일 물리 키를 기존 슬롯과 전용 IMC 양쪽에서 실행하지 않는다.
- 여러 입력이 같은 ActionId를 누르면 첫 Press와 마지막 Release만 제공자에 전달한다. 전환 후 누른 키를 새 동작으로 재생하지 않는다. 이전 위젯의 revision은 새로운 액션을 실행할 수 없다.
- 제공자의 ReadAction은 실제 사용 가능 여부/쿨타임/현재 키 이름을 읽는다. 키 이름 미제공 시 기존 SkillSlot의 현재 설정으로 표시한다.
- `FRPGActionBarActionView::UnavailableReason`은 제공자가 정한 지역화 표시 문구다. native 셀은 비활성 상태에서 액션 이름과 사유를 툴팁으로 표시하며, 사유가 비어 있으면 공통 사용 불가 문구를 표시한다. 활성 상태에서는 이전 사유를 숨긴다. 서버 내부 오류 문자열 대신 기능의 결과 enum을 표시 문구로 변환해 제공한다.
- 기본 좌측은 native 버튼 바, 별도 저작 위젯은 URPGActionBarWidget 파생 클래스. 중앙 클래스가 없으면 기존 직업 아이덴티티를 숨긴다. 새 중앙/좌측 위젯은 같은 context를 읽는다.
- `URPGActionBarIdentityWidget`은 중앙 Title/Status/Detail을 표시하는 네이티브 위젯이다. 값 갱신 때문에 프로필을 재설치하지 않으며, 저작 위젯도 같은 읽기 API를 사용할 수 있다.
- ARPGHUD는 기존 PlayerSkillBar/PlayerIdentity Canvas slot 배치를 사용한다. 원래 위젯과 직업 편성/쿨타임/우측 슬롯은 보존한다. 복귀 시 원래 위젯을 다시 표시한다.
- HUD가 교체되면 기존 presenter를 해제하고 현재 Pawn context를 다시 연결한다. 테스트용 HUD 등 두 canvas 영역이 없는 root는 교체 대상이 아니다.
- `ReleaseAllSkillInputs()`는 창 열기/입력 해제 시 기존 스킬과 제공자의 누른 액션을 함께 해제한다. 모드 전환 내부에서는 물리적으로 누른 상태를 유지하여 키를 놓기 전 새 동작이 실행되지 않는다.
- 사유/키/쿨타임은 같은 셀과 툴팁에서 현재 view를 읽어 갱신한다. 비활성 사유만 바뀔 때 프로필을 재설치하지 않는다. 포인터로 누르는 중 사용 가능 여부가 바뀌어도 Release는 전달하며, 제거된 셀의 툴팁/버튼 콜백은 weak 참조로 만료된다. 저작 위젯은 기존 `ReadAction` 반환값에서 같은 사유를 읽을 수 있다.
- native 셀은 생성할 때 전달된 아이콘 브러시를 복사한다. 이전 이미지가 상태 전환/위젯 재생성 뒤에도 Slate에 남아 있으면 엔진 `FDeferredCleanupSlateBrush`가 브러시와 텍스처의 GC/렌더 수명을 유지한다. 호출자의 배열/스택 주소를 계속 참조하지 않으며 원본 브러시의 후속 변경은 이미 생성한 셀을 바꾸지 않는다. 아이콘 교체는 불변 프로필의 교체 규칙을 따른다.

## Riding 담당 인계

`URPGRidingComponent`의 승인 Mounted 상태에 위 인터페이스를 구현한 어댑터를 연결한다. Profile의 액션은 Dash/Emote/Dismount 등 실제 Riding 명령으로 해석한다. 하차를 일반 스킬로 위장하거나 직업 슬롯 배열/영속 편성을 변경하지 않는다.

연결 순서는 프로필/어댑터 준비 → 소유 Pawn의 확정 Mounted 수신 → `Activate` → 입력을 기존 서버 명령으로 전달 → 확정 하차/종료의 `Deactivate`다. 어댑터 인스턴스를 보관해 해제 때 같은 인스턴스를 전달한다. HUD 재생성은 제공자나 쿨타임을 새로 만들지 않는다. 전환의 Release 콜백 안에서 다시 Activate하지 않으며, 전환/종료 중 실패한 활성화는 확정 상태의 수명에 맞춰 처리한다.

### 실제 상태 연결 — 2026-11-08

`URPGRidingActionBarSubsystem`은 로컬 플레이어에 자동 생성되는 UI 어댑터다. 현재 Controller/Pawn의 `URPGRidingComponent::OnChanged`를 구독하고 **컴포넌트의 현재 canonical 상태**를 다시 읽는다. Mounted에서는 공통 context에 Riding 프로필을 설치하고 OnFoot/소유 변경/종료에서는 자신이 설치한 제공자만 해제한다. 결과 영수증으로 상태를 추정하거나 복제 상태를 직접 쓰지 않는다.

현재 실제 서버 API가 있는 **Dismount 한 개**를 제공한다. 셀 클릭과 기존 네 번째 스킬 슬롯 입력은 모두 `RequestSetMounted(false, ...)`로 이어진다. 물리 키는 해당 슬롯의 현재 설정을 조회하며 별도 IMC나 기본 키 덮어쓰기를 추가하지 않는다. Dash/Emote와 탑승 호출/보관함·RidingShortcut 배정은 이 어댑터의 구현 범위가 아니다.

Riding의 native 좌측 바는 네 칸을 유지하며 하차를 네 번째 표시 위치에 둔다(`NativeSlotCount=4`, `DisplaySlot=3`, `SkillSlot=3`). 앞의 세 빈 칸에 실행할 수 없는 Dash/Emote를 추가하지 않는다. 표시 위치를 바꿔도 `SkillSlot`/`InputTag`·ActionId나 현재 물리 키 배정은 바뀌지 않는다. 사용자 직업 편성/우측 아이템 배열을 수정하지 않는다.

중앙에는 현재 정의의 표시 이름·“탑승 중”·“기본 이동 속도”를 제공한다. 이름은 현재 MountId/DefinitionVersion에 맞는 `GetPresentationDefinition()`의 메타데이터를 읽고, 없으면 공통 “탈것” 문구를 사용한다. 기본 속도는 canonical 복제 상태의 `Movement.Speed`이며, GAS 감속/외부 제한을 적용한 최종 속도나 실제 이동량을 뜻하지 않는다. UI에서 이동 프로필을 만들거나 보정 값을 수정하지 않는다. 하차/다른 프로필 전환에서는 원래 직업 아이덴티티를 복원한다.

제공자는 Component/Controller를 weak 참조하고 Epoch를 고정한다. HUD 재생성은 제공자를 교체하지 않으며, Epoch가 바뀌면 새 제공자로 교체해 이전 위젯 입력을 무효화한다. 다른 기능의 프로필이 활성화되어 있으면 상태 알림/수명 감시에서 되빼앗지 않는다. 해당 기능이 해제된 뒤 여전히 Mounted면 하차 프로필을 다시 표시한다. 향후 전체 Riding 제공자도 공통 `Activate`로 이 기본 프로필을 교체할 수 있다.

빙의/복제 이벤트를 즉시 처리하고, 로컬 Controller가 연결되어 있는 동안 0.1초 간격의 수명 감시로 늦은 컴포넌트 등록·해제도 확인한다. 이 감시는 상태/보유를 승인하거나 명령을 제출하지 않는다. Controller/Pawn/World/로컬 소유자가 바뀌거나 종료되면 구독과 입력 제공자를 해제하며 로컬 플레이어 종료 시 감시도 제거한다. 제품 해금/API·feature flag는 변경하지 않는다.

검증은 게시 `f6fef8c4` + 공통 UI/검사 소스22의 고정 사본에서 수행했다. 첫 전체 빌드의 `TObjectPtr` 포인터 추론 오류를 명시적 `APawn*`로 수정한 후, 최종 364단계 Editor 컴파일/전체 링크 종료0·DLL `8E13536B…6C546EE2` 실제 로드·native16 Success/검사 오류0/실행0을 확인했다. 이 중 Riding 연결 회귀는3개이며 기존 셀/전환/퀵슬롯/HUD 수명13개도 통과했다. 기존 에셋 경고140개·검사 전 `Condition failed`15개는 별도로 기록했고 그 외 로그 오류는0이다. 소스22와 실제 WBP_PlayerHUD 에셋 SHA는 검증 전후 일치했다.

새 회귀는 실제 Riding 컴포넌트의 RepNotify에 canonical 상태를 주입하여 구독/표시/원복·Epoch 교체/빙의/다른 제공자/수명/늦은 등록을 검사한다. 하차 입력은 **입장 세션이 없는 테스트 Pawn**에서 실제 요청 API→서버 거절/무효 Epoch 해제 경로까지 확인했다. 제품 보유 권한을 받은 플레이어의 성공 하차나 실제 소켓 복제/물리 입력/GPU 외형을 확인한 결과는 아니다. UI 밖에서의 상태 조작·테스트 해금·권한 부여 포트는 추가하지 않았다.

자기 임시 사본/OBJ/DLL/PDB/UserDir/cache 6,505,905,099 bytes(약6.06GiB)·Content 링크7개를 폐기했고 정리 예외는 없다. 원본 게임 DLL은 교체하지 않았다. 폐기한 모듈/입력의 해시는 당시 증거이며 로컬 최소 요약은 `Saved/BuildChecks/ActionBarRiding-20261108/Completion.json`이다. 유지할 검사 실행은 `Scripts/Tests/Run-ActionBarContextTests.ps1 -IncludeRidingBridge`이며, 후속 게시본/타 담당 진행 중 소스를 합친 전체 게임의 재검증과 구분한다.

## 검증 범위 — 2026-11-07

- 게시 `ff036751` + 본인 Source14의 독립 Editor 전체 빌드(863단계), 최종 수정 5단계 증분 빌드 성공. 실제 DLL `E135AF5D…F3821D`에서 `ProjectRPG.UI.ActionBar` 4개 + 기존 퀵슬롯 4개 + HUD 수명 2개, 총 10개 Success/검사 오류 0/실행 종료 0. 검증 전후 본인 소스 해시 일치.
- 실제 `WBP_PlayerHUD` 로드/영역 교체·복원, 별도 UMG 트리의 우측 보존/배치/입력 해제, 제공자 교체/소유 Pawn/재진입/종료/누른 키의 전환 및 복귀를 검사했다. 최초 실행은 테스트 viewport 누락으로 실패했고 fixture를 보완했다. 교체 위젯의 Slate 참조도 부착 완료까지 유지한다.
- 기존 에셋 관련 검사 경고 140개와 검사 시작 전 `Condition failed` 15개는 별도 기록했다. 후속 `40a7045a`까지의 게시 차이는 Riding 내부 세션·로비/HTTP/Backend로 본인 변경 경로와 겹치지 않으며, 그 전체 통합 바이너리를 다시 실행한 결과는 아니다.
- NullRHI/모형 제공자에 대한 검증이다. 실제 탑승·서버 명령·네트워크 전투·GPU 외형/물리 입력은 이 검사에 포함하지 않는다. 원본 게임 DLL/에셋은 교체하지 않았다.
- 자기 임시 사본/OBJ/DLL/PDB/캐시 6,508,179,401 bytes(약 6.06GiB) 및 junction 21개 정리 완료. 자동 승인 검토가 중복 로그 삭제를 `blocked by policy`로 거절해 1,301,487 bytes를 로컬 예외로 보관한다. UBT 공용 진단 Trace.uba는 삭제하지 않았다. DLL 해시는 당시 검증 증거이며 폐기한 바이너리의 재실행을 보장하지 않는다.
- 로컬 최소 요약: `Saved/BuildChecks/ActionBarContext-20261106/Completion.json`. 유지할 회귀 러너: `Scripts/Tests/Run-ActionBarContextTests.ps1`.

## 사용 불가 안내 확장 — 2026-11-08

`FRPGActionBarActionView`를 독립 헤더로 옮기고 `UnavailableReason`을 추가했다. 기존 `RPGActionBarProfile.h` 포함과 구조체의 모듈/이름은 유지한다. 기본 바는 `SRPGActionBarCell`을 사용하며 비활성 버튼 바깥의 활성 셀에 툴팁을 두어 현재 사유를 읽을 수 있다. 키/쿨타임/사유 갱신에는 프로필 재생성이 필요하지 않다. 매우 큰 쿨타임도 정수 overflow 없이 표시한다.

최종 검증은 게시 `43f5779a` + 본인 소스6 고정 사본이다. 실제 셀/USTRUCT의 독립 UHT·10단계 컴파일/링크 및 DLL `A82AC477…E13C4F2`에서 native3 Success/검사 오류·경고0/실행0, 실제 프로젝트 전체 UHT와 Widget/Cell/Context/검사 CPP4 집중 컴파일도 통과했다. 소스6 SHA 전후 일치·러너2 구문 검사를 확인했다. 첫 검사의 Slate 속성 갱신 누락은 fixture 수정 후 재검증했으며, 기동 `Condition failed` 15개는 별도 기록했다.

사용 불가 문구/클릭 차단·동일 툴팁의 실시간 사유 변경/활성 복귀·보관된 툴팁/버튼의 셀 제거 후 만료·쿨타임/키 갱신·누른 도중 차단된 입력 해제를 검사했다. 이번 검증은 실제 탈것 제공자의 연결·전체 게임 링크/실행·네트워크/GPU 증거가 아니다. 두 회차의 자기 임시 사본/OBJ/DLL/PDB/UserDir/cache 5,536,492,639 bytes(약5.16GiB)를 폐기했고 정리 예외는 없다. 공유 UBT 진단 trace는 보존했다. 결과/해시는 폐기 당시 증거로 `Saved/BuildChecks/ActionBarFeedback-20261108/Completion.json`에 기록했다.

유지할 집중 회귀는 `Scripts/Tests/Run-ActionBarCellTests.ps1`이다. 기존 전체 액션바 러너도 셀3개를 포함하도록 확장했으나 이번에 전체 게임 러너를 다시 실행한 것은 아니다. Riding 제공자는 차단 사유를 새 필드에 전달하면 된다.

## 탈것 중앙 정보와 Movement 계약 — 2026-11-08

Riding 프로필은 `URPGActionBarIdentityWidget`을 중앙 영역에 설치한다. 표시 내용은 메모리에 등록된 탈것 정의의 `DisplayName`(없으면 `탈것`), `탑승 중`, 복제된 `State.Movement.Speed`의 `기본 이동 속도`다. 승인된 기본 수치이며 GAS 보정 후 최종 속도나 현재 이동 속도와 구분한다. 정의 조회 때문에 에셋을 동기 로드하거나 상태/보유를 승인하지 않는다. 기존 이벤트·0.1초 수명 감시에서 이름을 갱신하고 표시 속성은 현재 view를 읽으므로 수치/탈것 변경 때문에 프로필·입력 revision을 다시 만들지 않는다.

중앙 이름/속도는 호스트 폭에 맞춰 줄바꿈하며 저작 WidgetTree가 있는 파생 위젯은 기존 트리를 사용한다. Epoch/제공자 교체·빙의 해제·종료는 이전 위젯의 읽기를 만료시킨다. 하차하면 기존 직업 아이덴티티로 복귀하고 우측 유틸리티 슬롯은 유지한다. 좌측은 기존 하차 액션 하나만 제공한다. Riding 검사 fixture도 Mounted의 필수 Movement 계약을 채우도록 갱신했다.

게시 `fb5b0176`의 독립 Editor 전체 892단계 빌드 후 최종 소스의 집중 CPP6·증분 4단계 전체 링크를 통과했다. 최종 DLL `3D8755F1…8F5FBCF00`을 실제 로드해 native18 Success/검사 오류0/실행0을 확인했다. 새 검사는 중앙 실제 Slate 텍스트·80px 호스트의 이름/속도 폭·동일 위젯의 속도/탈것 변경·Epoch 만료/하차 원복과 재진입/읽는 도중 빙의 해제를 포함한다. 기존 에셋 관련 경고140개·검사 전 `Condition failed`15개는 별도 기록했다. 최종 소스11과 실제 WBP_PlayerHUD SHA는 검증 전후 일치했다.

첫 실행은 검사18개를 통과했지만 이후 소스 변경 때문에 최종 근거에서 제외했다. 두 번째 빌드는 러너가 공용 Controller의 다른 담당 변경을 다시 복사하여 고정 사본에 없는 Narrative 헤더를 참조해 실패했다. Identity 모드는 본인11경로만 갱신하도록 고쳤고, 사본의 공용13경로를 처음 게시본으로 복구한 후 세 번째 실행에서 최종 코드를 검증했다. 원본 공용 소스와 게임 DLL/에셋은 변경하지 않았다. 이후 게시본의 Riding 지역/능력 차단은 소비 중인 상태/정의 조회 API를 유지하지만 그 전체 통합 바이너리를 이번에 실행한 결과는 아니다. 실제 권한 성공/소켓 복제/GPU 외형은 별도다.

자기 사본/OBJ/DLL/PDB/UserDir/cache 6,618,039,674 bytes(약6.16GiB)·Content 링크7개를 폐기했다. 공유 UBT trace는 보존하며 새 정리 예외는 없다. 폐기한 모듈/입력 해시는 당시 증거다. 로컬 최소 요약은 `Saved/BuildChecks/ActionBarIdentity-20261108/Completion.json`, 유지할 회귀 실행은 `Scripts/Tests/Run-ActionBarContextTests.ps1 -IncludeIdentity`다.

## 전환 후 아이콘 수명 — 2026-11-08

기존 native 바의 `IconBrushes` 배열 주소를 이미지에 넘기던 경로를 제거했다. 셀은 생성 중 아이콘을 엔진 `FDeferredCleanupSlateBrush`로 복사하고 이미지 속성이 이를 보유한다. 부모 셀/UMG 위젯이 먼저 제거돼도 남은 이미지가 잘못된 브러시 주소를 읽거나 텍스처를 GC로 잃지 않으며, 마지막 이미지 해제 후에는 엔진의 지연 렌더 정리로 자원을 반환한다. 프로필/입력/서버 상태와 아이콘의 외형·크기는 유지한다.

게시 `c04bdda7` + 명시 입력7 고정 사본에서 독립 UHT/10단계 빌드·링크, native 셀4 Success/검사 오류·경고0/실행0을 확인했다. 원본 브러시 변조/수명 종료와 부모 셀 제거 뒤에도 이미지 크기36×36·GC 후 텍스처 유지, 아이콘 없는 셀 및 기존 비활성 툴팁/쿨타임/입력 해제를 검사했다. 실제 프로젝트 전체 UHT와 Widget/Cell/Context/검사 CPP4 집중 컴파일도 통과했고 입력7 SHA는 전후 일치했다. 기동 `Condition failed`15개는 별도 기록했다. 실제 GPU 렌더 완료나 전체 게임 링크/네트워크 재검증은 이 집중 검사에 포함하지 않는다.

자기 사본/OBJ/DLL/PDB/UserDir/cache 2,815,238,659 bytes(약2.62GiB)를 폐기했고 정리 예외는 없다. 원본 게임 DLL/에셋과 공유 UBT trace는 보존했다. 로컬 최소 요약은 `Saved/BuildChecks/ActionBarIconLifetime-20261108/Completion.json`이며 폐기한 DLL 해시는 당시 증거다. 유지할 회귀는 `Scripts/Tests/Run-ActionBarCellTests.ps1 -IconLifetime`으로 실행한다.

## 프로필별 표시 위치와 빈 칸 — 2026-11-08

`DisplaySlot`은 표시 위치, `SkillSlot`/`InputTag`는 입력 배정이다. `TryBuildDisplayOrder`의 결과는 표시 위치→액션 배열 인덱스이며 빈 칸은 `INDEX_NONE`이다. 프로필 검증과 native 위젯이 같은 배치 판정을 사용하고, 실제 행 생성기는 빈 칸에 버튼/제공자 콜백을 만들지 않는다. 액션은 시각적 순서 대신 원래 ActionId로 실행된다. 고정 칸 수가0이면 필요한 만큼만 추론하고 기존 기본값(-1/0)은 이전 순서를 유지한다. 별도 저작 WidgetTree는 기존 표시 책임을 유지한다.

게시 `88e1021d` + 명시 입력12의 고정 사본에서 독립 UHT/12단계 빌드·링크와 native7 Success/검사 오류·경고0/실행0을 확인했다. 새 배치3개는 기본 순서 호환·명시 위치 우선/자동 혼합·뒤 빈 칸·빈 전체 행·중복/범위/칸 부족 거절 및 실제 Slate의 네 번째 버튼 위치/앞 세 빈 칸의 hit-test 제외·클릭 액션 인덱스를 검사한다. 기존 셀4개도 통과했다. 실제 프로젝트 전체 UHT와 관련 CPP7 집중 컴파일0·입력12 SHA 전후 일치를 확인했고 기동 `Condition failed`15개는 별도 기록했다. 전체 게임 링크·실제 HUD/GPU/소켓 RPC 재검증은 이 집중 검사에 포함하지 않는다.

자기 사본/OBJ/DLL/PDB/UserDir/cache 2,841,788,927 bytes(약2.65GiB)와 중복 proof를 폐기했고 정리 예외는 없다. 원본 게임 DLL/에셋과 공유 UBT trace는 보존했다. 폐기한 모듈 해시는 당시 증거다. 로컬 최소 요약은 `Saved/BuildChecks/ActionBarLayout-20261108/Completion.json`, 유지할 회귀는 `Scripts/Tests/Run-ActionBarCellTests.ps1 -SlotLayout`이다.
