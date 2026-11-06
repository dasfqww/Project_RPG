# 상태별 공통 액션바 — 2026-11-06

현재 단계: 공통 전환/입력/HUD 연결 구현 및 집중 검증 완료(2026-11-07). 실제 Riding 상태/이동/보유 기능과 제공자 연결은 Riding 담당 범위다.

## 연결 계약

- `UQuickSlotComponent::GetActionBarContext()`는 현재 Pawn에 귀속되는 공통 전환기다.
- `IRPGActionBarProvider`는 C++ 어댑터다. `GetActionBarPawn`, `GetActionBarProfile`, `ReadAction`, `PressAction`, `ReleaseAction`을 구현한다. Pawn/feature component는 weak pointer로 참조한다.
- `URPGActionBarProfile` Data Asset에는 ProfileId, Actions(최대 8개), LeftWidgetClass, CenterWidgetClass가 있다. 각 액션은 안정 ActionId, Label/Icon과 선택적 SkillSlot/InputTag를 가진다.
- 서버 확정 상태를 수신한 feature가 `Context->Activate(Provider)`를 호출한다. 같은 provider/profile은 멱등. 교체는 먼저 기존 입력을 해제한다.
- 하차/강제 종료 때 `Context->Deactivate(Provider)`. 다른 provider로 교체된 뒤 늦은 이전 해제는 무시한다. Pawn 종료는 공통 Shutdown.
- 상태를 중복 복제하지 않는다. 공통 전환기는 승인을 발급하거나 RPC를 보내지 않는다. Riding의 서버 상태/복제/리비전 가드가 유효할 때만 설치한다.
- Riding은 자신의 서버 전투/아이템 사용 가드를 계속 담당한다. HUD나 로컬 입력 차단을 서버 보안으로 취급하지 않는다.
- 현재는 기본 직업 UI 위에 하나의 제공자를 활성화하는 구조다. 제공자 교체는 지원하며, 우선순위 스택이나 여러 상태의 동시 합성은 포함하지 않는다.
- 활성 프로필 정의는 불변으로 취급한다. 쿨타임/사용 가능 여부는 `ReadAction`에서 갱신하고, 액션 목록/위젯 구성이 바뀌면 새 프로필/제공자로 교체한다.

## 입력과 표시

- 기존 스킬 슬롯 키는 현재 프로필의 SkillSlot으로 전달된다. 기존 능력 입력 태그는 InputTag로 전달되며 프로필에 없는 전투/아이덴티티 입력은 override 중 소비된다.
- Riding 전용 IMC/추가 키 제공자는 `PressAction(Source, ActionId, Context->GetRevision())` / `ReleaseInput(Source)`로 단일 경로에 합류한다. Source는 물리 입력별로 고유해야 한다. 동일 물리 키를 기존 슬롯과 전용 IMC 양쪽에서 실행하지 않는다.
- 여러 입력이 같은 ActionId를 누르면 첫 Press와 마지막 Release만 제공자에 전달한다. 전환 후 누른 키를 새 동작으로 재생하지 않는다. 이전 위젯의 revision은 새로운 액션을 실행할 수 없다.
- 제공자의 ReadAction은 실제 사용 가능 여부/쿨타임/현재 키 이름을 읽는다. 키 이름 미제공 시 기존 SkillSlot의 현재 설정으로 표시한다.
- 기본 좌측은 native 버튼 바, 별도 저작 위젯은 URPGActionBarWidget 파생 클래스. 중앙 클래스가 없으면 기존 직업 아이덴티티를 숨긴다. 새 중앙/좌측 위젯은 같은 context를 읽는다.
- ARPGHUD는 기존 PlayerSkillBar/PlayerIdentity Canvas slot 배치를 사용한다. 원래 위젯과 직업 편성/쿨타임/우측 슬롯은 보존한다. 복귀 시 원래 위젯을 다시 표시한다.
- HUD가 교체되면 기존 presenter를 해제하고 현재 Pawn context를 다시 연결한다. 테스트용 HUD 등 두 canvas 영역이 없는 root는 교체 대상이 아니다.
- `ReleaseAllSkillInputs()`는 창 열기/입력 해제 시 기존 스킬과 제공자의 누른 액션을 함께 해제한다. 모드 전환 내부에서는 물리적으로 누른 상태를 유지하여 키를 놓기 전 새 동작이 실행되지 않는다.

## Riding 담당 인계

`URPGRidingComponent`의 승인 Mounted 상태에 위 인터페이스를 구현한 어댑터를 연결한다. Profile의 액션은 Dash/Emote/Dismount 등 실제 Riding 명령으로 해석한다. 하차를 일반 스킬로 위장하거나 직업 슬롯 배열/영속 편성을 변경하지 않는다.

연결 순서는 프로필/어댑터 준비 → 소유 Pawn의 확정 Mounted 수신 → `Activate` → 입력을 기존 서버 명령으로 전달 → 확정 하차/종료의 `Deactivate`다. 어댑터 인스턴스를 보관해 해제 때 같은 인스턴스를 전달한다. HUD 재생성은 제공자나 쿨타임을 새로 만들지 않는다. 전환의 Release 콜백 안에서 다시 Activate하지 않으며, 전환/종료 중 실패한 활성화는 확정 상태의 수명에 맞춰 처리한다.

## 검증 범위 — 2026-11-07

- 게시 `ff036751` + 본인 Source14의 독립 Editor 전체 빌드(863단계), 최종 수정 5단계 증분 빌드 성공. 실제 DLL `E135AF5D…F3821D`에서 `ProjectRPG.UI.ActionBar` 4개 + 기존 퀵슬롯 4개 + HUD 수명 2개, 총 10개 Success/검사 오류 0/실행 종료 0. 검증 전후 본인 소스 해시 일치.
- 실제 `WBP_PlayerHUD` 로드/영역 교체·복원, 별도 UMG 트리의 우측 보존/배치/입력 해제, 제공자 교체/소유 Pawn/재진입/종료/누른 키의 전환 및 복귀를 검사했다. 최초 실행은 테스트 viewport 누락으로 실패했고 fixture를 보완했다. 교체 위젯의 Slate 참조도 부착 완료까지 유지한다.
- 기존 에셋 관련 검사 경고 140개와 검사 시작 전 `Condition failed` 15개는 별도 기록했다. 후속 `40a7045a`까지의 게시 차이는 Riding 내부 세션·로비/HTTP/Backend로 본인 변경 경로와 겹치지 않으며, 그 전체 통합 바이너리를 다시 실행한 결과는 아니다.
- NullRHI/모형 제공자에 대한 검증이다. 실제 탑승·서버 명령·네트워크 전투·GPU 외형/물리 입력은 이 검사에 포함하지 않는다. 원본 게임 DLL/에셋은 교체하지 않았다.
- 자기 임시 사본/OBJ/DLL/PDB/캐시 6,508,179,401 bytes(약 6.06GiB) 및 junction 21개 정리 완료. 자동 승인 검토가 중복 로그 삭제를 `blocked by policy`로 거절해 1,301,487 bytes를 로컬 예외로 보관한다. UBT 공용 진단 Trace.uba는 삭제하지 않았다. DLL 해시는 당시 검증 증거이며 폐기한 바이너리의 재실행을 보장하지 않는다.
- 로컬 최소 요약: `Saved/BuildChecks/ActionBarContext-20261106/Completion.json`. 유지할 회귀 러너: `Scripts/Tests/Run-ActionBarContextTests.ps1`.
