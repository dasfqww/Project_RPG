# 대검 임시 4종과 폭주 구현 — 2026-11-02

[전사 회의](GREATSWORD_WARRIOR_DESIGN_MEETING_20261102.md)와 [아이덴티티 회의](GREATSWORD_IDENTITY_DESIGN_MEETING_20261102.md)의 중심 4종을 기존 SkillContainer에 연결했다. 회의 문서의 미구현 표기는 당시 이력이며, 아래 내용이 이번 적용 결과다. **최대/진입 100, 분노 스킬 사용당 50, 해당 시전 피해 ×1.15는 첫 시험값**이다.

| 스킬 | 명시적 분류 | 재사용한 실행/애니메이션 |
|---|---|---|
| 돌진 베기 `AssultBlade` | 일반 | 기존 돌진 실행, `AM_PlayerSkill:AssultBlade` |
| 도약 강타 `JumpSmash` | 일반 | 기존 도약 실행, `AM_PlayerSkill:JumpSmash` |
| 차지 슬래시 `ChargeSlash` | 분노 | 기존 Charge 실행, `AM_PlayerChargeSkill:Prepare → Loop → Attack` |
| 회전 베기 `WhirlWind` | 분노 | 기존 Toggle 실행, `AM_PlayerToggleSkill:Default → Toggle → End` |
| 폭주 진입 | 아이덴티티 | `AM_ActivateRage`, GAS Montage task |

몽타주 경로는 모두 `/Game/Blueprints/Character/Player/Anim/Montage/` 아래다. 기존 입력·스킬 클래스·피해 승인·실행 정책·쿨타임을 사용한다. 스킬 이름이나 실행 정책에서 분노 여부를 추론하지 않는다. `RPGSkillDefinition`의 `bGreatSwordRageSkill`, `GreatSwordRageCost`, `GreatSwordRageDamageMultiplier`를 시전별 runtime snapshot으로 복사한다. 차지/회전 Definition 두 에셋에만 분노 분류를 저장했다.

## 상태와 비용 계약

- 해금된 현재 대검 직업·살아 있는 현재 Pawn·대검 장착·게이지 100·행동 가능한 대기 상태에서 수동 진입한다. 현재 편성된 분노 스킬이 없으면 거절한다.
- 진입 때 게이지 100을 유지하고, 현재 퀵슬롯에 등록되고 사용 권한이 있는 분노 Definition의 쿨타임만 초기화한다. 공통 cooldown GE의 정확한 SourceObject까지 대조하므로 일반 스킬·미등록 보유 스킬은 초기화하지 않는다.
- 기존 `Player.Status.Identity.GreatSword.Ready` 태그를 폭주 상태로 사용한다. 지속시간·주기 소비·자연 감소가 없으며, 폭주 중 재입력으로 재초기화하지 않는다. 진입 몽타주 종료/취소와 폭주 상태의 수명은 분리한다.
- 분노 스킬의 성공한 commit 직후, 몽타주/타격 전에 **한 시전당 한 번** 50을 지불하고 배율 1.15를 고정한다. 부분 차지도 강화되며 회전의 시간·타격·적 수로 추가 지불하지 않는다. 취소/빗나감으로 환급하지 않는다.
- 0이 되면 폭주 태그를 제거한다. 마지막 시전은 끝까지 고정 배율과 수급 금지를 유지한다. 같은 스킬도 정상 쿨타임 후 재사용할 수 있다.
- 일반 상태에서는 기존의 유효 스킬 적중당 시전 1회 20, 기본 연계 완주 10 수급을 유지한다. 폭주 중 시작/승인한 기본 공격·일반 스킬·분노 스킬은 수급하지 않는다. 일반 스킬은 폭주 비용/피해 강화 대상이 아니다.
- 폭주 중 편성 복원을 막으며, 사망·Pawn 수명 변경·던전 전투 권한 회수·능력 제거·대검 외 장착 전환 시 상태/게이지를 회수한다. 기존 서버 권한과 시전별 피해 검증을 유지한다.

별도 두 번째 게이지, 공격속도/이동속도 보너스, 유지 타이머, 수동 장기입력 해제, 추가 4종은 이번 구현에 포함하지 않았다.

## 검증 결과와 재실행

정상 `Project_RPGEditor Win64 Development` 빌드 종료 0, 선택한 Source/Plugins 입력 1,382개 불변을 확인했다. 최종 모듈 SHA256은 `26CADDA33ABFCBC50D8A75399E0A7BEFFB0F82431DEF5416F9D35D30EE52B42D`다. 검증은 정상 DLL의 닫힌 사본을 실제 로드했고, 검증 종료 후 그 사본은 폐기했다. 해시는 당시 증거이며 삭제된 사본의 재실행을 보장하지 않는다.

- 연결 전 기존 몽타주 5종을 실제 PIE SkeletalMeshActor에서 재생했다. 각 몽타주의 재생 위치와 `hand_r`, `hand_l`, `foot_r` 포즈 변화, 차지/회전의 시작·유지·종료 섹션을 확인했다. NullRHI 검사이므로 렌더링 외형 평가나 실제 전투 적중 검사는 아니다.
- `ProjectRPG.Skill.GreatSword.BerserkLifecycle` 성공, 검사 오류 0/기존 장착·태그 로그 경고 5, UE 종료 0. 실제 ASC·직업 grant·퀵슬롯·스킬 활성화로 미편성 거절, 등록 대상만 쿨초, 20초 대기/자연 감소 없음, 100→50→0, 중복 commit 방지, 취소 무환급, 같은 차지 재사용, 마지막 시전 배율/수급 금지, 회전 유지 중 추가 소비 없음, Pawn 재사용/사망 회수를 확인했다.
- 분류 에셋 저장 후 새 프로세스에서 일반 2종/분노 2종·50/1.15 재로드 일치, 추가 저장 0, UE 종료 0을 확인했다.
- 기존 성장 전투 E2E probe/러너의 100 소비·25%·마지막 수급 기대값을 새 계약으로 갱신했다. CPP 컴파일과 PS 구문 검증까지 수행했으며, 그 전체 네트워크 E2E는 이번에 재실행하지 않았다. 부분 차지/전체 회전의 실제 피해와 다인 복제까지 새 검사가 입증한 것은 아니다.

초기 컴파일의 잘못된 지역 변수 참조, 미진행 에디터 프리뷰/짧은 PIE 제한 시간, 같은 시각 재진입으로 기존 입력 속도 제한을 만난 fixture 실패는 수정 후 최종 성공과 구분해 기록했다. 입력 속도 제한은 변경하지 않았다. UE 기동 단계의 기존 오류 로그와 검사 결과 오류 0도 구분한다.

```powershell
# 정상 Editor 모듈 빌드 후, 최신 조율 표에서 에셋/사본 취득 자원을 예약한다.
powershell -NoProfile -File Scripts/Tests/Run-GreatSwordPrototype.ps1
powershell -NoProfile -File Scripts/Tests/Run-GreatSwordPrototype.ps1 -Playback -PythonScript Scripts/Editor/PlayGreatSwordPrototype.py
powershell -NoProfile -File Scripts/Tests/Run-GreatSwordPrototype.ps1 -Native
powershell -NoProfile -File Scripts/Tests/Run-GreatSwordPrototype.ps1 -PythonScript Scripts/Editor/ConfigureGreatSwordPrototype.py
# 최초 분류 작성이 필요한 경우에만 위 Configure 명령에 -Apply 추가.
```

증거는 로컬 `Saved/BuildChecks/GreatSwordPrototype-20261102/`의 `BuildResult.json`, `Verification.proof.json` 및 실행별 작은 결과에 있다. 최종 재생 `0b357f40…`, native `520836c4…`, 재로드 `e08d4451…`를 사용한다. 러너는 자기 프로세스 종료와 파일 사용 해제 뒤 자기 Temp만 제거하며 Content/Plugins junction을 따라 원본을 삭제하지 않는다. 공유 Binaries/Intermediate/DDC와 다른 담당 산출물은 정리 대상이 아니다.

실행 사본/DLL/캐시 5,398,658,880 bytes(약 5.03GiB)를 폐기했으며 실행별 Temp 잔여는 없다. 자동 승인 검토가 `Implementation/` 중복 작성본과 `PrepareIdentity.py`, `PrepareImplementation.py`의 삭제를 일반/명시 경로 방식 모두 `blocked by policy`로 거절했다. 합계 216,413 bytes만 정리 예외로 보관하며 Git에 포함하지 않는다. 상세 차단 사유는 제공되지 않았다. 정책상 삭제가 허용되면 이 담당이 제거할 대상이며 다른 담당의 정리 예외와 구분한다.

## 실제 투지/폭주 HUD 연결 후속

기존 `WBP_PlayerIdentity`는 추출 당시의 `OnCurrentRageChanged`/활성 이벤트 체인을 유지해 현재 `CurrentIdentityGauge`와 폭주 태그를 직접 관찰하지 않았다. `URPGGreatSwordIdentityWidget`을 모듈의 부모로 연결하고 이 19개 레거시 구독 노드만 제거한다. 부모 HUD, 공용 상태 바 템플릿, 기존 바/아이콘의 배치·스킨 및 `InterpUp` 애니메이션 객체는 유지한다.

표시는 현재 Pawn의 ASC attribute delegate와 `Player.Status.Identity.GreatSword.Ready` 태그를 읽는다. 같은 게이지에서도 태그 변경만으로 표시가 갱신된다. 위젯 생성 전에 도착한 값도 즉시 읽고, 직업/스킬 복원 알림에 다시 반영한다. 소스 교체·사망·종료에서는 표시와 이전 구독을 정리한다. UI에 소비/강화 판정이나 별도 자원·자연 감소 타이머를 두지 않는다.

- 일반 상태: `투지 50/100`
- 만충 상태: `투지 충전 완료 100/100` — 행동 가능 여부와 무관한 충전량 안내
- 폭주 상태: `폭주 100/100` 또는 `폭주 50/100`, 붉은 바와 기존 진입 애니메이션
- 미해금 상태: `고유 능력 미해금`

시간 제한이 없는 폭주에 템플릿의 미사용 시간 문구를 표시하지 않는다. `ConfigureGreatSwordIdentityHUD.py`는 기본 읽기 전용이며 `-RPGConfigureGreatSwordIdentityHUD`를 지정했을 때 모듈 한 에셋만 저장한다. 직전 레이아웃·그래프·애니메이션과 부모 HUD/템플릿 해시를 대조하고, 이미 적용된 에셋에는 추가 저장하지 않는다.

문구는 이 모듈의 native 표시 단계에서 기본 위치보다 32px 위에 배치한다. 기존 폭주 아이콘의 상단 테두리가 문구 아래쪽을 가리던 문제를 피하며, 공용 상태 바 템플릿이나 기존 바·아이콘의 에셋 배치는 바꾸지 않는다.

최종 정상 빌드 `619D6E9F…395AB8BA`/선택 입력1,389개 불변을 확인했다. `ProjectRPG.UI.GreatSwordIdentity`의 상태/수명과 실제 작성 스킨 2개 사례가 Success(검사 오류0, 기존 에셋 경고108)이며, `a4a55e71…`가 문구 가시성 보완까지 포함한 결과다. 새 Cmd 재로드는 native 부모·기존 레이아웃·애니메이션 일치/추가 저장0이다.

보완 전 `3db17d03…`의 실제 HTTP/Memory 입장·Iris에서는 서버 GE로 0→만충100→폭주100→폭주50→종료0을 자극해 소유자 UMG 값/문구 데이터·바 비율5단계·1920×1080 배치와 UE 종료0/0·오류0을 확인했다. 실제 스킬 소비를 다시 실행한 검사는 아니다. 이때 PNG 검토로 템플릿이 문구를 숨기는 것을 발견해 **문구를 명시적으로 표시하고 좁은 기존 바에 맞게 16pt로 보완**했다. 최종 가시성/시간 문구 숨김은 작성 스킨을 사용하는 native 회귀로 확인했다.

선행 가시성 보완 직후에는 공유 링크 입력 및 독립 PIE 초기화 실패로 최종 픽셀 확인을 마치지 못했다. 2026-11-03 후속 GPU 캡처에서 실제 문구와 기존 아이콘 테두리의 중첩을 발견해 위의 문구 위치 보정을 추가했다. 기존 정상 모듈의 닫힌 링크 입력에 이 CPP 하나만 교체 컴파일·링크한 독립 DLL로 실제 작성 UMG를 1920×1080 렌더링했다. 투지0→만충100→폭주100→폭주50→종료0의 문구 5장을 확인했고, 모든 상태에서 문구 하단935/아이콘 상단952로 17px 간격을 확인했다. 같은 DLL의 상태/수명·작성 스킨 native2도 Success(검사 오류0/기존 에셋 경고108), GPU/검사 UE 종료0/0이다. 실제 PIE/네트워크 재실행이나 원본 정상 출력 교체를 뜻하지 않는다.

이 후속 결함 수정의 유지할 변경은 표시 CPP1·이 계약·본인 조율 행뿐이다. 임시 관찰 모듈/러너·사본·PNG·proof는 로컬 `Saved/BuildChecks/GreatSwordIdentityVisual-20261103/`에 기록하고 검증 전용 파일을 게시하지 않는다. 최종 실행은 `55ac1c34…`이며, 앞선 미보정 캡처는 최종 결과로 취급하지 않는다.
