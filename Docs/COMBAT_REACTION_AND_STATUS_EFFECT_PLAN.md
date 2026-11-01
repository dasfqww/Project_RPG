# 전투 피격 반응 및 상태이상 개발 계획

검토 기준: 2026-10-26, 현재 프로젝트 C++와 설치된 UE 5.8 소스.
상태: 설계와 진행 기록. 일반 피격 데이터는 14절, 스턴은 16절, 공용 수평 넉백은 17절, 공유 소스 통합·현재 프로젝트 적용/검증은 18절을 따른다. 에어본·다운·기상 구현과 검증 범위는 19절에 기록한다. 12~13절은 기반 구현과 중간 검증 기록이다.

공용 둔화의 설정·속도 계산·집계·검증 진행은 20절에 기록한다. 첫 지속 피해인 공용 화상은 23절에서 구현·검증했다.

2026-10-26 추가 요청에 따라 지속 피해 전에 공용 면역 분류와 스킬 실행 중 면역을 연결한다. 구현·검증 범위는 21절을 따른다.

공용 면역의 현재 프로젝트 통합은 22절, 화상 지속 피해의 정책·현재 모듈 통합·검증은 23절에서 완료했다.

몽타주 구간별 면역의 저작 방식·서버 수명·검증 범위는 24절에서 완료했다. 운영 몽타주의 실제 구간 배치와 전용 연출은 별도 콘텐츠 작업이다.

기간형 일반 경직의 공용 데이터·우선순위·서버 제어와 현재 프로젝트 검증은 25절에서 완료했다. 이전 절의 미구현 목록은 해당 검증 당시의 기록이다.

대표 공격 두 개의 실제 경직 데이터 연결과 부여/표시 검증은 26절에 기록한다. 이 수치는 초기 조정값이다.

에어본의 20도 경사면·낭떠러지 아래 착지 회귀와 분리 DLL 검증은 27절에 기록한다. 에어본 중 늦은 참가와 초기 시각 보정 작업은 28절에 기록한다. 이동 발판의 정상 운반과 외부 이동 구분·기본 착지/회복 검증은 36절을 따른다. 발판의 실제 Iris/플레이어 보안·회전/물리 발판은 별도다.

2026-10-30 정상 Editor 모듈에 에어본 이동 시계 수정을 통합하고, 같은 DLL의 집중 native 10개와 원래 Guardian 공격의 서버/피격자/관찰자 Iris 검증을 통과했다. 최신 결과·정리·인계 범위는 35절을 따른다. 32~34절의 정상 모듈 반영 대기는 이 범위에서 해소됐다.

## 1. 목표와 현재 결정

플레이어와 NPC가 같은 피격·상태이상 규칙을 사용한다. 서버가 적용 여부를 결정하고, 대상별 입력·AI·이동·애니메이션 연결부가 그 결과를 실행한다.

합의된 방향은 공통 규칙의 모듈화와 작은 기능 단위의 단계적 연결이다. 진행 순서는 실제 연결 조사 → 양방향 일반 피격 표시 → 스턴 → 넉백 → 에어본·다운·기상 → 지속 상태이상이다. 클래스 이름과 세부 API는 제안이며 실제 코드 조사 결과에 맞춰 정한다.

여기서 모듈화는 책임과 의존성의 분리를 뜻한다. 첫 구현부터 새 UE 플러그인, 별도 DLL, 모든 대상을 지원하는 범용 프레임워크를 만들 필요는 없다. 현재 Project_RPG 모듈의 공용 코드와 필요한 컴포넌트로 시작한다.

## 2. 재검토에서 발견한 문제와 수정 방향

| 기존 문서의 문제 | 구현 시 영향 | 수정 방향 |
|---|---|---|
| 단계 설명과 첫 완료 조건에 HitReact·스턴·강제 이동이 섞임 | 어느 기능까지 해야 완료인지 불명확 | 0~5단계의 산출물과 완료 조건을 분리 |
| 피격 몽타주를 재생하면 연출만 바뀐다고 가정 | 공격 몽타주 중단, Notify·Root Motion 변경으로 실제 전투가 바뀔 수 있음 | 첫 단계는 게임 동작에 영향 없는 표시로 제한하고 슬롯/취소 콜백을 조사 |
| 주기 피해 GE에 기존 피해 계산을 붙이면 된다고 서술 | 기존 승인 함수는 Instant GE와 동기 실행의 피해 guard를 요구 | 지속 효과 수명과 매 회차의 승인된 즉시 피해를 나눠 별도 설계 |
| MoveSpeed GE만 적용하면 둔화가 된다고 가정 | 현재 C++에는 속성 복제와 실제 MaxWalkSpeed 소비 경로의 연결이 확인되지 않음 | 속성→서버/클라이언트 이동 계산 연결과 타깃 락 충돌부터 확인 |
| GAS 상태와 공통 모듈이 각각 지속시간/중첩을 관리할 여지 | 두 타이머·태그·UI 상태가 서로 어긋남 | 지속 효과의 원본은 GAS, 별도 런타임은 이동/연출과 자기 효과 핸들만 관리 |
| 기존 피격 이벤트와 새 경로의 전환 규칙 부재 | 같은 적중에서 피격 반응이 두 번 발생 | 선택한 공격 경로별로 기존 이벤트와 새 전달 중 하나만 실행 |
| 태그·GameplayEvent·복제 상태를 같은 알림처럼 취급 | 이벤트 미전달, 짧은 연속 피격 유실, 늦은 참가자의 상태 누락 | 순간 표시와 지속 상태의 전송·복원 계약을 구분 |
| Player/NPC 어댑터에 제어를 맡기기만 함 | 서버 이동 처리나 다른 시스템의 AI 정지를 덮을 위험 | 서버 제어 집행과 소유한 제약만 해제하는 규칙 명시 |
| 반응 Ability를 일반 입력 Ability와 동일하게 취급 | 맞는 횟수가 플레이어 활성화 요청 제한에 포함되거나 반응이 잘못 차단될 수 있음 | 서버 발동 반응과 사용자 입력 제한의 경계를 확인하되 생존·권한 검사는 유지 |

이 문제들은 계획의 방향을 부정하지 않지만, 이전 문구 그대로 구현을 시작하면 잘못된 전제가 코드에 들어갈 수 있다. 아래 내용을 구현 기준으로 사용한다.

## 3. 현재 코드에서 확인한 기반

- Player와 일반 NPC는 ARPGBaseCharacter가 생성한 Pawn 소유 ASC를 사용한다. 네이티브 초기화는 InitAbilityActorInfo(this, this)다. PlayerState 소유 ASC라고 가정해 새 시스템을 작성하지 않는다.
- 일반 NPC와 보스는 생성자에서 ASC 복제를 켜고 Minimal 모드를 설정한다. Player의 실제 BP/CDO 설정은 별도 확인이 필요하다. NPC에는 원격 몽타주 수신을 위한 BeginPlay ActorInfo 초기화가 있다.
- 피격 전달 경로에는 RPGGameplayAbility의 호환 경로인 Shared_Ability_HitReact와 SkillContainer의 설정된 HitReactTag 등이 있다. 이름만 보고 모두 같은 수신 Ability라고 판단하지 않는다. D1 근접 Ability의 Shared_Event_HitReact 전달은 아래 D1 정리에서 함께 제거됐다.
- 설치된 엔진의 SendGameplayEventToActor는 해당 프로세스의 ASC.HandleGameplayEvent를 호출한다. 이 호출 자체를 다른 클라이언트로 보내는 네트워크 전송으로 취급하지 않는다.
- 기존 방향 계산 함수와 Player/NPC HitReact BP·몽타주 파일이 있다. UNPC_HitReaction의 C++ 생성자만으로 실행 동작을 판단할 수 없으므로 실제 BP 그래프·태그·슬롯·몽타주 설정을 조사해야 한다.
- 2026-10-25 D1 정리에서 구형 Shield Bash/Ground Breaker를 포함한 D1 전용 스킬·근접·투사체·장판 구현을 제거했다. 여기에 있던 임시 LaunchCharacter·지연 스턴·Loose Tag·DisableMovement·타이머 후 MOVE_Walking 경로도 폐기했다. 현재 공통 피격·상태이상 구현은 이 D1 경로를 기반으로 삼지 않는다.
- FRPGDamageExecutionGuard.Supports는 즉시 효과, 지정된 단일 피해 Execution, 제한된 GE 구성만 허용한다. 승인 guard의 수명도 동기 호출 범위다.
- MoveSpeed/MoveSpeedPercent는 복제되지만 해당 OnRep는 속성 알림만 수행한다. Player/NPC 생성자와 TargetLock Ability에는 MaxWalkSpeed를 직접 쓰는 코드가 있다. 속성 소비를 BP에서 수행하는지는 미확인이다.
- 플레이어 이동에는 던전 권한 회수 전용 차단이 있다. 보스에는 전멸·리셋·기믹에 따른 AI/이동 정지가 있다. 일반 스턴이 이 기능들의 해제를 수행해서는 안 된다.

이 목록은 소스 확인 결과다. 실제 콘텐츠의 양방향 피격 성공이나 현재 바이너리의 실행 결과를 뜻하지 않는다. 오래된 D1 미완료 체크리스트는 조사 단서로만 사용한다.

## 4. 공통 모듈의 책임

### 4.1 게임 규칙을 세 층으로 나눈다

| 층 | 역할 | 상태의 원본 |
|---|---|---|
| 공격 결과와 적용 정책 | 서버가 인정한 적중/상태 적용 요청, 대상의 저항, 반응 선택, 중복 판정 | 기존 서버 전투 경로와 서버가 읽은 공격/대상 데이터 |
| 지속 상태와 제어 | 스턴·둔화·침묵 등의 기간, 중첩, 태그, 정화 및 능력 제한 | 서버 ASC에 적용된 GameplayEffect |
| 반응 실행과 표시 | 넉백/공중/착지/기상, 방향별 피격 표시, AI·입력 연결 | 서버 이동/반응 실행 및 그 결과를 읽는 표현 경로 |

HitReact와 기절은 서로 다른 종류다. 첫 HitReact는 방향별 피격 표시이며 행동 불가를 뜻하지 않는다. 실제 경직은 진행 중 행동 취소와 이동/능력 제한을 포함하는 제어 기능으로 별도 완료해야 한다.

### 4.2 공유할 코드와 대상별 연결부

- 공용 데이터: 반응 종류, 서버 방향 기준, 필요한 GE/표현 참조, 대상 저항 정책. 기존 Skill Definition과 보스 Ability에서 같은 공용 설정 형식을 사용하되 각 데이터의 원본은 하나만 둔다.
- 공통 정책: 적용 가능성, 피해와 반응 면역의 구별, 반복 적용 규칙과 결과. PlayerController나 Behavior Tree를 직접 참조하지 않는 계산/조회 함수로 시작한다.
- 공통 런타임: 필요하면 Pawn에 URPGCombatReactionComponent를 두어 현재 반응 실행과 효과/이동 핸들을 조정한다. GAS의 활성 효과 목록·중첩 수·종료 타이머를 다시 만드는 저장소는 만들지 않는다.
- GAS 연결: 양쪽 모두 같은 적용·해제 경로를 사용한다. 초기 지원 대상은 현재 ASC와 CharacterMovement를 가진 캐릭터다. ASC 없는 액터, 차량 등에 대한 일반화는 후속 요구가 생길 때 한다.
- 이동 연결: Player와 NPC가 사용하는 CharacterMovement 기능은 공유한다. 플레이어의 네트워크 입력 처리와 NPC의 Path Following/AI 명령 제어처럼 실제 차이가 있는 부분만 분리한다.
- 표현 연결: 스켈레톤·무기·AnimBP별 몽타주 매핑은 데이터로 둔다. 지원하지 않는 방향의 fallback과 표시 생략을 명시하고, 잘못된 애니메이션이 전투 상태를 변경하지 않게 한다.

인터페이스 수는 실제로 다른 구현이 생기는 경계에 맞춘다. 하나의 구현만 필요한 기능마다 새 어댑터 계층을 만들지 않는다.

## 5. 서버의 적용 계약

### 5.1 호출 순서

1. 플레이어 입력 또는 서버 AI가 기존 Ability/전투 경로를 시작한다.
2. 기존 서버 검증이 발동·팀·거리·피격점·예산·현재 권한을 확인하고 피해를 처리한다.
3. 선택한 공격 경로가 승인 결과와 서버 소유 반응 설정을 공통 처리부에 넘긴다. 실패한 피해 함수 호출을 성공으로 해석하거나 클라이언트가 지정한 대상/반응/세기를 그대로 사용하지 않는다.
4. 피해 및 Ability 콜백 뒤 대상의 생존·같은 Pawn/ASC·세계·실행 수명이 여전히 유효한지 확인한다. 치명타격으로 죽은 대상에는 새 스턴·피격 몽타주를 시작하지 않는다.
5. 반응 면역·현재 제어 상태·재적용 정책을 평가한다. 피해 결과와 반응 결과는 따로 반환한다.
6. 해당 기능의 GE 또는 이동/표현 실행을 시작한다. 후속 콜백이 같은 실행을 재진입해도 두 번 실행되지 않게 한다.

피해가 이미 적용된 뒤 반응이 거절되거나 표시 에셋이 없어도 피해를 롤백하지 않는다. 여러 효과의 적용을 DB 트랜잭션처럼 원자적이라고 가정하지 않는다. GE 적용 실패 뒤 강제 이동을 실행할지 등 복합 반응의 부분 실패 정책은 해당 기능 단계에서 명시한다.

### 5.2 반복 적중과 방향

- 동일 적중의 기준은 서버 실행 식별자 + 타격 구간/회차 + 대상 Pawn 수명이다. 기존 Ability의 타격 중복 제거·예산을 우선 재사용하고, 새 전달 경로의 재진입/이중 전송만 필요한 범위에서 막는다.
- 정상적인 다단히트는 회차가 달라야 한다. 단순히 같은 대상/같은 프레임이라는 이유로 서로 다른 공격까지 제거하지 않는다.
- 네트워크 표현 식별자는 게임플레이 중복 방지의 권한 근거로 쓰지 않는다. 서버 승인 사실을 클라이언트/BP가 임의로 만들어 전달할 API를 추가하지 않는다.
- 방향은 승인된 타격 시점의 피해자 방향과 공격/충돌 방향으로 서버에서 결정한다. 클라이언트가 지연 후 공격자의 현재 위치로 다시 계산하지 않는다. 투사체·장판은 공격자 위치와 실제 충격 방향이 다를 수 있으므로 별도 방향 정책과 영벡터 fallback을 둔다.

### 5.3 피해 없는 상태 효과

첫 단계는 기존의 승인된 피해 적중에만 붙인다. 속박·침묵처럼 피해가 없는 공격은 이후 서버의 별도 상태 적용 승인 경로를 사용한다. 그 경로도 능력/팀/범위/상태 적용 자격을 검증한다.

HP 감소량 0과 빗나감은 동일 개념이 아니다. 방어·보호막·피해 면역과 제어 면역의 조합은 후속 기능별로 정한다. 피해 guard를 통과시키려고 가짜 피해를 만들지 않는다.

## 6. 피격·제어 상태의 수명

### 6.1 표시만 하는 HitReact

- 첫 단계의 게임플레이 변화는 없다. 입력·Ability·AI·이동 상태를 잠그거나 공격을 취소하지 않는다.
- 몽타주 슬롯/그룹 충돌, Root Motion, 게임플레이 Notify, OnInterrupted/OnCancelled 콜백을 조사한다. GAS 몽타주 재생은 기존 재생과 충돌할 수 있으므로 자동으로 안전한 연출이라고 보지 않는다.
- 초기 기본안은 안전하게 표시할 수 있는 대기/이동 상태에서만 표시하고, 공격·회피·사망 등과 충돌하면 표시를 생략한다. 적합한 additive/슬롯이 이미 있으면 활용한다. 새 풀바디 피격으로 행동을 끊어야 한다면 제어 기능 단계로 분류한다.
- 표시의 완료는 로컬 연출 수명이다. 클라이언트가 끝났다고 서버의 게임플레이 상태를 해제하지 않는다.
- 적용 거절, 이미 처리한 적중, 표현 에셋 없음, 현재 애니메이션과 충돌해 표시 생략을 진단에서 구분한다.

### 6.2 스턴 등 기간 제어

- GE가 기간·중첩·상태 태그를 소유한다. 지속시간 관리용 Loose Tag와 독립 타이머를 추가해 같은 상태를 이중 관리하지 않는다.
- 반응 런타임은 자신이 적용한 GE 핸들과 필요한 제어 핸들만 추적한다. 효과 제거/억제/만료/정화/재적용 알림을 처리하고, 한 효과 종료로 다른 출처의 같은 상태까지 지우지 않는다.
- 태그를 붙이는 것만으로 일반 이동·AI·이미 실행 중인 Ability가 모두 멈춘다고 가정하지 않는다. 새 Ability 발동 차단, 현재 Ability 취소, 이동 제약은 각각 적용한다.
- 서버가 발동하는 피격 Ability는 사용자 입력 횟수 제한과 구분한다. 현재 RPGGameplayAbility의 ShouldApplyServerInputEnforcement/ShouldApplyServerActivationRateLimit 설정을 확인하고, 맞은 횟수를 입력 폭주로 집계하지 않게 한다. 이 조정으로 사망·던전 권한 회수 검사를 우회해서는 안 된다.
- 취소 대상은 명시된 전투 Ability 범위로 제한한다. 비취소 Ability와 상태 면역이 충돌할 때의 정책을 정한다. CancelAllAbilities, 모든 GE 제거, 던전 권한 회수 함수를 일반 스턴 처리로 사용하지 않는다.
- UI·카메라·로비/메뉴 입력까지 일괄 차단하지 않는다. 플레이어 공격/이동 제약을 서버에서도 집행한다.
- 겹친 제약은 출처별로 추적한다. 죽음·권한 회수·맵 종료·Pawn 교체 시 현재 세대의 콜백과 핸들만 정리한다. 기절 종료가 죽은 캐릭터를 걷게 하거나, 보스 기믹이 멈춘 AI를 재개시키면 안 된다.
- GE/이동 핸들은 서버 내부의 소유 추적에 사용한다. 네트워크 식별자로 그대로 사용하지 않는다. 같은 Pawn을 재사용하는 재도전·encounter 리셋도 새 실행 세대로 처리해 이전 콜백을 무효화한다.

### 6.3 넉백·에어본·다운

- 이동 실행에는 서버 소유 실행 식별자와 명확한 교체/중단 규칙을 둔다. 해당 실행의 이동 작업·몽타주·보안 승인 핸들만 종료한다.
- 플레이어의 소유 클라이언트(Autonomous Proxy)와 다른 관찰 클라이언트(Simulated Proxy)를 따로 확인한다. 서버 위치 복제만으로 입력 예측·이전 SavedMove 재생·보정이 해결됐다고 판단하지 않는다.
- 기존 URPGPlayerMovementComponent의 서버 이동 처리와 통합한다. 스턴 중 보낸 이동 명령, 지연 도착한 명령, 기존 Root Motion이 제약을 우회하지 않는지 확인한다. 일반 스턴에 던전 퇴장 전용 RPC 차단을 그대로 복사하지 않는다.
- 정상 강제 이동은 기존 보안 컴포넌트의 BeginMovementAuthorization/EndMovementAuthorization 핸들로 필요한 시간·거리만 승인한다. 승인 범위를 넓히거나 다른 실행의 승인을 해제하지 않는다.
- 넉백 구현은 LaunchCharacter 또는 GAS Root Motion Source의 실제 양방향 동작을 비교해 선택한다. 둘을 동시에 적용하거나 별도 위치 Multicast를 병행하지 않는다.
- 입력 이동 차단과 중력/외부 강제 이동 허용을 구분한다. DisableMovement로 MOVE_None을 주면 공중 이동 자체를 막을 수 있다. 종료 시 MOVE_Walking을 무조건 복구하지 않는다.
- 공중 → 착지 → 다운 → 기상은 서버 충돌·이동 상태로 전환한다. GE의 고정 시간 만료나 클라이언트 몽타주 종료만으로 착지를 판단하지 않는다. 벽·천장·경사·낭떠러지·착지 실패와 사망/중도 취소를 다룬다.
- NPC 어댑터는 새 Path Following/공격 명령을 제약하고, 기존 목표를 무조건 다시 실행하지 않는다. 제약 해제 때 사망·전멸·기믹·다른 제어 효과가 남아 있는지 확인한다.

## 7. 지속 상태이상의 별도 연결

### 7.1 둔화

GE로 MoveSpeed 속성을 변경하는 것에 더해 실제 CharacterMovement 속도 계산과 연결한다. 서버, 소유 클라이언트, NPC의 소비 경로를 확인한다.

TargetLock의 MaxWalkSpeed 변경/캐시 복구와 충돌하지 않도록 속도 계산의 원본을 정한다. 둔화가 끝날 때 저장했던 과거 속도를 덮어써 다른 버프·장비·타깃 락 변경을 지우지 않는다. 이 연결을 확인하기 전에는 “둔화 구현 완료”로 기록하지 않는다.

### 7.2 화상·중독·출혈

현재 승인 함수에 Periodic/Duration 피해 GE를 그대로 넣는 방법은 사용하지 않는다. 프로젝트의 피해 guard는 동기 Instant 실행을 요구하며, 현재 피해 계산은 공격자/대상의 생존과 권한도 확인한다.

후속 설계의 기본안은 기간 GE가 상태 수명을 소유하고, 서버가 그 활성 상태에 연결된 회차 실행을 관리해 각 회차를 승인된 즉시 피해로 처리하는 방식이다. 회차 실행 스케줄은 GE의 별도 만료 타이머가 아니며, 효과 제거/정화/교체 시 함께 종료한다.

해당 단계 진입 전에 다음을 결정한다.

- 최초 부여 시 팀/범위/능력 검증, 이후 회차의 출처·대상·상태 핸들·세대 검증.
- 부여 후 거리가 멀어져도 효과가 남는지. 기존 근접 HitResult를 위조하거나 사거리 제한을 전역 완화해 해결하지 않는다.
- 공격자 사망/제거 뒤에도 피해가 남는지와 어그로·처치/보상 귀속. 현행 코드의 살아 있는 Source 요구와 다르면 별도 변경·검증이 필요하다.
- 각 회차의 피해 상한, 회차 수와 간격, 중첩별 예산, 서버 지연 시 누락 회차 처리. 밀린 피해를 한 프레임에 몰아 적용하지 않는다.
- 현재 능력치 사용/부여 시점 스냅샷, 치명타, 자원 획득, 타격 부가 효과의 회차별 허용 여부.
- 틱 피해가 매번 HitReact나 새 상태 효과를 다시 부여해 무한 연쇄를 만들지 않도록 허용 규칙을 둔다.

저항·중첩·정화는 상태 정의별 정책이다. 정화는 제거 가능한 분류와 자기 대상 효과만 처리하며, 버프나 영구 장비 효과를 일괄 제거하지 않는다.

## 8. 네트워크와 표현 계약

| 전달할 것 | 계약 |
|---|---|
| 순간 피격 표시 | 기존 GAS 몽타주 또는 Cue/명시적 표현 신호 중 한 경로를 선정. 같은 몽타주를 Cue와 OnRep 양쪽에서 중복 재생하지 않음 |
| 스턴 등 지속 상태 | GAS 태그/효과를 게임 규칙의 원본으로 사용. 관찰자에게 필요한 단계·남은 시간만 최소 표현 데이터로 제공 |
| 강제 이동과 다운 상태 | 서버 CharacterMovement가 위치를 결정하고 반응 단계가 연출을 안내. 서로의 복제 도착 순서를 가정하지 않음 |

- NPC의 Minimal 모드에서는 클라이언트가 모든 Active GE와 지속시간을 조회한다고 전제하지 않는다. Player의 실사용 복제 설정과 등록된 서브오브젝트 설정도 0단계에서 확인한다.
- 지속 상태의 보조 스냅샷은 서버 효과/실행으로부터 계산한 읽기 전용 표현이다. 별도의 게임플레이 권한 원본으로 사용하지 않는다.
- 순간 이벤트는 패킷 손실/연속 갱신에서 생략되거나 합쳐질 수 있다. 첫 표시 단계는 최신 유효 피격만 재생하는 정책을 허용한다. 모든 순간 이벤트의 무손실 전달을 완료 조건으로 삼지 않는다.
- 지속 상태는 늦은 참가·재관련성·ActorInfo/AnimInstance 지연 준비 뒤에도 현재 상태로 수렴해야 한다. 지난 피격·이미 만료한 다운 상태를 다시 시작하지 않는다.
- 대상 Pawn 세대, 실행 번호, 서버 시각으로 오래된 갱신을 무시한다. 클라이언트 종료 시각은 표시 용도이며 서버 GE 제거/권한 해제의 근거가 아니다.
- 첫 단계에서 피격 예측은 도입하지 않는다. 이후 필요하면 서버 거절·교체 시 취소, 예측 키, 중복 Cue/몽타주 방지를 별도 기능으로 다룬다.
- 서버와 화면이 같은 프레임에 시작·종료해야 한다고 요구하지 않는다. 각 검증 실행의 지연·손실·프레임 조건과 상태 수렴/만료 후 잔류 허용 기준을 실행 전에 기록한다.

## 9. 단계별 작업과 완료 조건

### 0단계 — 실제 연결 조사

산출물은 공격 경로 → 승인 피해 함수 → 피격 이벤트 → 수신 Ability → 몽타주/AnimBP의 연결표다. Player/NPC ASC의 소유·복제·초기화, 기존 취소 태그·이동 제약도 함께 기록한다.

그 결과로 대표 Player 공격 1개와 일반 NPC 공격 1개, 표시 방법, 교체할 기존 이벤트 위치를 정한다. Shield Bash는 현재 fallback의 참고 사례이며 현재 실사용/부여 여부를 확인하기 전 첫 플레이 콘텐츠로 고정하지 않는다.

기존 HitReact BP가 이미 공격 취소·입력 제한을 수행한다면 이를 표시 전용 경로로 바꿔 기존 경직을 없애지 않는다. 첫 단계에 적합한 다른 경로를 선택하거나, 기존 기능의 제어 계약을 먼저 정리해 2단계 전환 대상으로 기록한다.

실제 BP를 읽지 못했다면 그 항목을 미확인으로 남긴다. 소스 조사 완료와 에셋 연결 확인 완료를 구별한다.

### 1단계 — 양방향 피격 표시

공통 요청/판정과 대상별 표현 연결만 구현한다. 경직·이동 제한·스턴·넉백을 넣지 않는다. 적용 대상은 선택한 두 공격 경로에 한정한다.

완료 조건:

- 전용 서버 + 클라이언트 A/B에서 A의 공격 → NPC 피격, NPC 공격 → A 피격을 확인하고 B는 두 상황의 관찰자가 된다. 2인 PvP를 전제하지 않는다.
- 서버 승인 적중만 한 번 전달되고, 같은 적중의 중복 호출은 두 번 표시하지 않는다. 다른 정상 타격 회차를 중복으로 제거하지 않는다.
- 정한 방향과 대상에 표시되며, 무효/치명타격에는 생존 피격을 재생하지 않는다.
- 대기 상태에서 실제 표시를 확인한다. 공격·회피·사망과 충돌하는 상황에서는 정한 생략/표현 정책을 따르고 기존 Ability·피해·Root Motion·이동 결과가 바뀌지 않는다.
- 늦게 도착한 표시가 새 Pawn이나 사망 상태를 덮지 않고, 표시 종료 뒤 관련 연출만 정리된다.

이 단계 완료는 경직/상태이상 기능의 완료가 아니다. 이동 오차나 스턴 면역은 해당 기능 단계에서 평가한다.

### 2단계 — 스턴 한 종류

Duration GE와 공통 제어 계약을 추가하고 Player/NPC 양방향에 적용한다. 갱신/연장/중첩 중 초기 정책 하나를 선택해 고정한다.

완료 조건은 서버 발동/이동 제한, 선택된 활성 전투 Ability 취소, NPC AI 제약, 클라이언트 표시, 중첩된 다른 제약 보존이다. 조기 정화·자연 만료·사망·재적용·Pawn 교체와 늦은 입력을 포함한다. 후속 넉백이 아직 없어도 이 단계는 독립 완료할 수 있다.

### 3단계 — 넉백

공통 제어와 이동 실행을 연결한다. Shield Bash가 적합한 실사용 경로이면 기존 fallback을 이 경로로 교체한다.

완료 조건은 소유/관찰 클라이언트의 이동 수렴, 정상 이동 보안 승인, 스턴/공격 Root Motion과의 충돌 정책, 벽 충돌·중단·재피격·사망 후 자기 제약만 정리하는 것이다. 서버가 LaunchCharacter를 호출했다는 로그만으로 완료하지 않는다.

### 4단계 — 에어본·다운·기상

공중·착지·다운·기상의 서버 전환과 각 대상의 표현 자산을 연결한다. 필요한 애니메이션을 확정한 뒤 작업한다. 기상 입력/쿨타임·피격 중 재기상 등은 콘텐츠 기획이 있을 때 추가한다.

### 5단계 — 지속 효과와 보스 정책 확장

둔화, 이후 지속 피해 한 종류씩 추가한다. 각 효과의 속성 소비/회차 피해, 정화·중첩, 원인 추적, 상태 UI를 확인한다.

보스·엘리트의 지원 반응과 면역/저항은 데이터로 조정한다. 보스 무력화/그로기 게이지는 별도 기능이다. 기존 보스 전멸·기믹 정지 상태와의 충돌을 확인한 뒤 보스 공격에 확대한다.

각 단계는 관련 코드 확인/자동화와 실제 2인 연결을 우선한다. 지연·손실·중도 퇴장/재접속은 해당 상태 수명과 직접 관련되는 범위로 추가한다. 이후 4인 자연 전투, Steam/Shipping 검증을 별도 기록한다. 테스트 수를 늘리기 위한 새 프레임워크나 무관한 전체 회귀를 각 단계마다 요구하지 않는다.

## 10. 전환·데이터 검증·결정 관리

- 선택한 공격의 이전 HitReact 전송과 새 공통 전송을 동시에 켜지 않는다. 공용 피해 helper에 반응을 무조건 추가하면 기존 caller의 이벤트가 중복될 수 있으므로 caller별 연결표를 먼저 작성한다.
- 이전 수신 BP가 맡던 피해·행동 취소·상태 적용도 함께 조사한다. 새 표시 경로로 전환하면서 이 게임플레이 동작이 소실되는 변경은 1단계의 단순 모듈화로 처리하지 않는다.
- 기존 콘텐츠는 명시적으로 선택한 경로부터 전환한다. 실패 시 해당 연결만 이전 경로로 되돌릴 수 있게 하며 새 반응과 이전 fallback의 동시 실행은 금지한다.
- 설정 검증은 사용하는 태그, GE 정책, 기간/힘/회차의 유한값과 범위, 대상 지원 능력, 몽타주 스켈레톤/섹션/슬롯/Notify를 검사한다. 잘못된 설정에서 공격자 임의 수치로 보정하지 않는다.
- 실전 적용·면역 거절·중복 무시·표시 생략·효과 제거 사유를 개발 진단에 남긴다. 서버 허용 결과와 각 클라이언트의 표시 결과를 혼동하지 않는다.
- 공통 모듈화와 Player/NPC 양방향 지원은 확정된 방향이다. 미결 항목은 해당 단계에 들어갈 때 기존 기획과 실제 콘텐츠를 바탕으로 정하고, 변경 이유와 확인 범위를 이 문서에 기록한다.

| 시점 | 결정할 사항 |
|---|---|
| 0단계 완료 전 | 대표 공격/실제 ASC 설정/이벤트 수신 BP/슬롯·전송 방식/새 경로 단독 실행 위치 |
| 1단계 착수 전 | 충돌 시 표시 생략, 방향 fallback, 연속 표시 합치기 정책 |
| 2단계 착수 전 | 스턴 재적용·면역, 취소 가능 전투 Ability, 제어 출처 합성/해제 |
| 3~4단계 착수 전 | 이동 방식·보안 예산·교체 우선도·벽/공중/착지/기상 정책 |
| 5단계 착수 전 | 속도 계산 원본, DoT 회차 권한·출처 수명·귀속·중첩·정화 |

## 11. 근거 파일

- [캐릭터 공통 ASC 생성과 초기화](../Source/Project_RPG/Private/Character/RPGBaseCharacter.cpp)
- [일반 NPC의 Minimal 복제와 ActorInfo](../Source/Project_RPG/Private/Character/RPGNonPlayerCharacter.cpp)
- [보스의 ASC 설정과 기믹/전멸 이동 정지](../Source/Project_RPG/Private/Character/RPGBossCharacter.cpp)
- [플레이어 서버 이동과 권한 회수 경계](../Source/Project_RPG/Private/Component/RPGPlayerMovementComponent.cpp)
- [핸들 기반 이동 승인 API](../Source/Project_RPG/Public/Component/RPGSecurityValidationComponent.h)
- [피해 승인 함수](../Source/Project_RPG/Private/FunctionLibrary/RPGAbilityFunctionLibrary.cpp), [동기 Instant 피해 guard](../Source/Project_RPG/Private/Security/RPGDamageExecutionGuard.cpp)
- [현재 피해 계산의 출처 생존 검사](../Source/Project_RPG/Private/GameplayEffect/GE_ExecCalc_TakeDamage.cpp)
- [플레이어 승인 피해/피격 전달](../Source/Project_RPG/Private/Ability/RPGPlayerGameplayAbility.cpp), [SkillContainer의 피격 전달](../Source/Project_RPG/Private/Skill/RPGGameplayAbility_SkillContainer.cpp), [호환 피격 전달](../Source/Project_RPG/Private/Ability/RPGGameplayAbility.cpp)
- [Shield Bash 넉백·스턴 fallback](../Source/Project_RPG/Private/Ability/Player/RPGPlayerSkillAbilities.cpp)
- [이동 속성 OnRep](../Source/Project_RPG/Private/Attribute/RPGAttributeSet.cpp), [TargetLock의 속도 변경/복구](../Source/Project_RPG/Private/Ability/PlayerGameplayAbility_TargetLock.cpp)
- [기존 네트워크 검증 기록](SECURITY_ABUSE_TESTING.md), [과거 이식 계획](D1_PVE_MIGRATION_STATUS_AND_ROADMAP.md)
- 설치 엔진 소스: D:/UE_5.8/Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/의 Private/AbilitySystemBlueprintLibrary.cpp, Private/AbilitySystemComponent_Abilities.cpp, Public/AbilitySystemComponent.h.

## 12. 구현 진행 — 2026-10-25

이 절은 최초 기반 구현 시점의 기록이다. 후속 콘텐츠 연결과 현재 프로젝트 적용 결과는 13절을 따른다.

### 실제 콘텐츠 연결 조사

실행 중인 에디터의 MCP로 CDO, 시작 데이터, Ability 트리거/취소 태그, 그래프 노드와 몽타주 트랙을 읽었다. 당시 다른 PIE가 실행 중이어서 읽기 조회만 수행했고 BP/에셋을 저장하지 않았다.

| 방향/대상 | 확인한 실제 연결 | 전환 시 보존할 기존 동작 |
|---|---|---|
| 플레이어 기본 공격 → Guardian | GA_Player_DefaultAttack → RPGComboSkillAbility/RPGPlayerGameplayAbility의 Shared.Event.Hit 처리 → ApplyLegacyDamageToServerHit → 설정된 Shared.Ability.HitReact → 시작 데이터에 부여된 GA_Guardian_HitReact → GA_NPC_HitReact_Base | NPC.Ability 취소/차단, 공격자 쪽 회전, GE_NPC_UnderAttack 적용, 피격 재생과 재질 표시 |
| Guardian → 플레이어 | DA_Guardian의 GA_Guardian_Melee1/2 → GA_NPC_MeleeAttack_Base.HandleApplyDamage → BP_ApplyEffectSpecHandleToTarget 성공 분기 → Shared.Event.HitReact → DA_Player의 GA_Player_HitReact | 플레이어 공격·장비·방어·회피 취소와 Player.Ability 차단, 피격 재생과 재질 표시 |
| 플레이어 표시 자산 | BP_Player의 SK_CharM_Ram/ABP_Player, AM_Player_HitReaction의 UpperBodySlot, Front/Back/Left/Right 방향 분기 | 몽타주에 AN_CustomPlaySound_C Notify가 있다. 참조 클립의 Root Motion은 꺼져 있지만 원본은 현재 표시 전용 검사를 통과하지 않는다. |
| Guardian 표시 자산 | AM_Guardian_HitReact의 DefaultSlot, React1/React2 설정, 전방 Light Front/Front Alt 두 클립 | 몽타주에 같은 사운드 Notify가 있고 두 클립의 Root Motion이 켜져 있다. 네 방향 피격이 아니므로 표시 전용 자산과 명시적인 fallback이 필요하다. |

Player CDO의 ASC와 Actor는 복제·등록된 서브오브젝트 사용이 켜져 있다. Player ASC의 네이티브 설정은 엔진 기본 Full이며 실제 런타임 복제 모드의 별도 관측은 하지 않았다. 일반 NPC의 Minimal 설정과 ActorInfo 초기화는 네이티브 코드에서 확인했다.

원본 조회 결과는 Saved/CombatReactionDev/audit-index.json, audit-values.json, audit-graphs.json, audit-startup.json, audit-final.json, audit-npc-base.json에 있다. read_graph_dsl의 빈 결과를 그래프가 없다는 증거로 사용하지 않고 find_nodes/get_node_infos 결과로 확인했다.

### 공통 기반의 현재 구현

- ARPGBaseCharacter가 생성하는 URPGCombatReactionComponent를 추가했다. Player/NPC가 같은 수신 코드와 방향 계산을 사용한다.
- 서버 승인 이후의 기존 네이티브 전달 지점에 선택 경로를 추가했다. 공격 Ability와 대상 컴포넌트 양쪽에서 선택한 경우에만 새 경로를 사용하며, 기본값은 이전 반응 경로다.
- 새 경로는 서버에서 확정한 방향·몽타주·섹션과 제한된 유효시간을 일시적인 Unreliable Multicast로 보낸다. 시퀀스가 중복/역행하거나 대상이 죽음·권한 회수·다른 재도전 revision인 경우 표시하지 않는다. 늦게 준비된 Mesh/ASC를 기다리며 예전 이벤트를 재생하지 않는다.
- 몽타주와 참조 Sequence의 Notify/Root Motion이 있으면 사용하지 않는다. 기존 몽타주/GAS 몽타주가 있거나 스켈레톤이 다르면 표시를 생략하며, 실행 중인 공격을 취소하지 않는다. 정리 시 자신이 재생한 몽타주 인스턴스만 중단한다.
- 승인 적중 식별자의 중복 기억은 컴포넌트별 최근 64개로 제한한다. 기존 전투 경로의 타격 예산/중복 검증을 대체하지 않는다. 이동 제약·스턴·넉백이나 GAS 지속 효과는 아직 추가하지 않았다.

Guardian의 실제 공격 BP는 피해 승인 뒤 자체 이벤트를 별도로 보내므로, 네이티브 배열 피해 helper에 새 경로를 넣은 것만으로 이 공격의 전환이 완료되지 않는다. 해당 BP의 성공 분기에서 전달 경로 하나를 선택하도록 연결하는 작업이 남는다. 현재 BP의 제어 동작을 제거하는 전환은 별도 제어 계약을 확인한 후 진행한다.

### HealthComponent 정리

체력 수치·최대 체력·사망 판정은 기존 `URPGAttributeSet`/ASC를 사용한다. 별도의 체력 facade를 공통 반응 시스템의 의존성으로 유지하지 않는다.

- `URPGHealthComponent`의 h/cpp, BaseCharacter의 기본 서브오브젝트/멤버/getter, `LyraHealthComponent → RPGHealthComponent` 클래스 리다이렉트를 제거했다.
- 보스전 재도전의 기존 Pawn 복구는 `ARPGPlayer::RestoreEncounterPresentation`으로 옮겼다. 서버에서 GAS 생존 상태와 권한을 확인하고, 사망 Ability만 취소한 뒤 시작 시 캡슐 충돌/애니메이션 pause를 복원한다. 복구 revision은 Player가 복제하고 BaseCharacter의 가상 조회를 통해 공통 반응에 제공한다. 다른 Ability/GE/장비 초기화는 추가하지 않았다.
- GameMode의 호출과 `TestRaidBossProgression.py`의 체력 조회를 변경했다. 반응 컴포넌트는 HealthComponent를 참조하지 않는다.
- 활성 Content의 클래스 import를 조사해 `BP_RPGNPCBase`, `BP_RaidNPCBase`, `BP_RaidSwordMan`, `BP_RaidSwordManVariant`, `BP_RaidSwordManSummonMechanic`, `BP_RaidSwordManCombo` 6개를 전환했다. 임시로 구 클래스를 유지한 빌드에서 재저장한 후 클래스를 삭제하고, 새 빌드에서 여섯 BP의 재로드/컴파일·공통 수신 컴포넌트 존재·구 클래스 부재를 확인했다. 원본은 `Saved/Backups/HealthComponentCleanup-20261025/`에 백업했다.
- 비활성 `Plugins/GameFeatures/GladiatorCore/Content`의 D1/Lyra 참조 콘텐츠는 이식하지 않았다. 활성 프로젝트에서 구 컴포넌트를 다시 요구하게 만들지 않는다.

### 확인 범위와 다음 작업

- 작업용 별도 프로젝트 경로에서 UHT 성공을 확인했고, 변경한 C++ 7개 파일의 개별 MSVC 컴파일이 성공했다. 첫 컴포넌트 컴파일의 const 인수 오류는 수정한 뒤 재컴파일했다.
- 일반 빌드는 사용자 AppData의 UBT 설정/추적 파일 쓰기 제한으로 실패했다. 작업용 별도 프로젝트와 UBA 저장 경로로 런타임 모듈의 UHT·컴파일·링크 성공을 확인했다(`HealthCleanup-FinalBuild.log`, 테스트 수정 후 `HealthCleanup-VerifiedBuild.log`). 전체 Editor 타깃/Shipping 패키징 성공으로 확대하지 않는다. 빌드 완료 뒤 사용자 폴더 `Trace.uba` 저장 제한은 별도로 남는다.
- 클래스 삭제 후 BP 검증: `Saved/CombatReactionDev/Verify-1f68a080307f470d97a5ce98ab795ef1/Result.json` 성공/종료 0. 새 DLL에서 6개 BP를 로드·컴파일했고 이 검증에서는 패키지를 저장하지 않았다.
- 최초 집중 native 검사는 6개 중 재도전 2개와 반응 3개가 성공했고, 원본 몽타주를 표시 전용으로 사용할 수 있다는 테스트 가정 1개가 실패했다. `ReactionMontageInspection.json`의 사운드 Notify/Root Motion 조사로 원인을 확인했다. 검사를 완화하지 않고 원본 거절을 검증하며, 메모리의 Notify 없는 플레이어 몽타주 복사본으로 정상 dispatch를 검사하도록 수정했다. 저장된 표시 전용 콘텐츠나 실제 재생의 증거는 아니다.
- 수정 후 `ProjectRPG.Combat.Reaction` 4개와 `ProjectRPG.Boss.BattleRetry`/`BattleRetryRequest` 2개가 모두 성공했다(총 6개, 종료 0). 방향·시퀀스·옵트인/생존/승인 식별자·원본 자산 거절과 transient 정상 전달·동일 Pawn의 복구 revision 증가를 확인했다. 증거는 `Saved/CombatReactionDev/Test-7f8d17d9824643f49deda5bca296c289/Result.json`이다.
- 에디터 프로세스가 없는 상태에서 모듈 manifest 일치와 검증 DLL 해시를 확인하고, 기존 런타임 DLL/PDB를 백업한 뒤 검증된 별도 빌드의 모듈을 프로젝트 Binaries에 적용했다. DLL SHA-256은 `77365307AFDAF3954611EDD9A188B3642C5F9501B43DCB1FBBF40E35F59B5C60`이며 `Saved/CombatReactionDev/AppliedModule.json`에 기록했다. 원래 프로젝트의 전체 Editor 타깃을 새로 빌드했다는 뜻은 아니다.
- 적용된 DLL에서 기존 `Run-BossMapE2ETest.ps1 -DeathAbilities -SkipSDKSetup -Port 18857 -TimeoutSeconds 150`을 실행해 전용 서버 역할 프로세스와 원격 클라이언트 2개가 통과했다(종료 0). 실제 플레이어 사망 GAS, 늦은 참가, 전멸/재도전, 같은 Pawn의 HP·캡슐·애니메이션·입력 준비 복구와 새 보스를 확인했다. 세 프로세스의 Iris 및 Net/Iris Error·assert·ensure 부재, 완료 뒤 추가 2초 검사도 통과했다. 증거: `Saved/Tests/BossMap/4174bc7edc094d919ef5cce012738508/`의 로그와 `BossMap.proof.json`. 시작 무기는 두 클라이언트 모두 0개였으며, NullRHI 검사다. 장비 착용 복구·새 피격 표시의 실제 재생·Shipping 검증으로 확대하지 않는다.
- 활성 Content 전체의 `.uasset/.umap`에서 구 `RPGHealthComponent` 클래스 import가 없고, C++/Scripts/Config에는 제거 확인 테스트의 클래스 이름 외 의존성이 없음을 확인했다. 수정한 Python의 구문 검사도 통과했다. 모듈 링크를 막던 기존 `RPGDungeonDepartureRestartE2E.cpp`의 UE_LOG 분기 중괄호와 `RPGOnlineLobbyPartyView.cpp`의 멤버 이름 가림도 최소 수정했다.
- 1단계의 실제 양방향 네트워크/애니메이션 완료로 표시하지 않는다. 기존 BP의 제어 반응 분리, 안전한 표시 자산, 실제 슬롯 사용과 NPC 공격 BP의 전달 경로 전환이 남아 있다. 이 조건을 만족한 대표 경로만 선택 적용한 뒤 서버와 두 클라이언트에서 관찰한다.

## 13. 일반 피격 콘텐츠 연결 — 2026-10-25

이 절의 GA 자식 BP/체크박스 방식은 중간 구현 기록이다. 현재 구성은 14절의 데이터 정책과 공용 표시 프로필로 전환한다. 아래 실행 결과는 당시 바이너리의 증거다.

### 선택한 전환 범위

기존 Guardian/Player HitReact는 공격 취소·차단을 수행한다. 이 동작을 표시 전용 반응으로 대체하지 않는다. 1단계에는 아래 **별도의 일반 피격 공격 변형**을 사용하며, 기존 시작 데이터와 무기의 기본 공격 지정은 유지한다.

| 선택한 공격 | 기존 부모 | 승인 후 경로 |
|---|---|---|
| `/Game/Blueprints/Combat/Reactions/GA_Player_LightHitAttack` | `GA_Player_DefaultAttack` | 기존 네이티브 HandleApplyDamage의 피해 승인 → 공용 수신 컴포넌트 |
| `/Game/Blueprints/Combat/Reactions/GA_Guardian_LightHitAttack` | `GA_Guardian_Melee1` | 기존 NPC BP HandleApplyDamage → 네이티브 피해 승인 및 경로 결정 → BP의 기존 이벤트 분기 |

두 자식 BP만 공격의 `bUseSharedHitPresentation`을 켠다. 기존 부모 공격은 false를 유지한다. Player/Guardian BP의 수신 프로필을 준비해도 기존 공격이 저절로 표시 전용 공격으로 바뀌지 않는다. 플레이용 데이터에서 새 변형을 선택할 때에는 해당 공격이 입력·AI·이동을 제약하지 않는 일반 피격임을 기준으로 사용한다.

### NPC BP 승인 결과와 표시 자산

- `BP_ApplyEffectSpecHandleToTarget`은 실제 피해 성공 여부와 별도로 `bOutUseLegacyHitReaction`을 반환한다. 기존 서버 피해 승인에 성공한 같은 네이티브 호출 안에서만 새 경로를 시도한다. BP가 승인 사실을 임의로 만들어 전달하는 API는 추가하지 않는다.
- `GA_NPC_MeleeAttack_Base.HandleApplyDamage`의 기존 성공 후 이벤트 전송에 이 bool의 Branch를 연결한다. 기존 성공 분기의 다른 효과는 유지하고, 새 경로가 선택된 적중에서는 이전 Shared.Event.HitReact를 보내지 않는다. 피해 실패를 성공이나 표시 실패로 바꿔 보고하지 않는다.
- `AM_Player_LightHit`은 기존 네 방향/UpperBodySlot을 사용하되 몽타주 Notify가 없는 별도 복사본이다. 원본 몽타주와 네 클립은 수정하지 않는다.
- `AM_Guardian_LightHit`은 별도 복사한 두 클립의 Root Motion을 끄고 첫 프레임 root lock을 강제한다. 원본 공격/피격 클립은 유지한다. Guardian은 네 방향 콘텐츠가 없으므로 `React1`을 명시적인 공통 fallback으로 사용한다.
- 자산 구성/전환은 `Scripts/Editor/ConfigureSharedHitPresentation.py`, 감사한 NPC 그래프의 Branch 삽입은 기존 EditorUtilities의 제한된 편집 함수로 재현한다. 스크립트는 원본 백업과 변경 목록을 `Saved/Backups/SharedHitPresentation/Manifest.json`에 남긴다.

### 검증 계약

기존 bossmap 검사 컴포넌트에 명시적 개발 모드 `RPGBossMapE2ELightHit`을 추가한다. 서버 역할 프로세스와 원격 클라이언트 두 개에서 Player/NPC 대기 상태의 네 방향/Guardian fallback, 동일 승인 신호 재전달, 무적 대상, 치명 타격 후 생존 피격 미재생을 확인한다. 네트워크 검사에 사용하는 능력 부여와 피해 이벤트는 서버 테스트 자극이며 Enhanced Input·공격 타이밍·자연 전투 검증을 대신하지 않는다. 클라이언트는 자기 몽타주 인스턴스의 실제 재생/섹션과 수신 시퀀스를 관찰해 보고한다. Net/Iris Error·assert·ensure와 BP 실행 오류를 검사하고 고정된 DLL 해시를 기록한다.

실행 스크립트는 `Scripts/Network/Run-SharedHitPresentationTest.ps1`다. NullRHI에서 네이티브 몽타주 재생을 관측하며 GPU 화면 품질, 공격/회피 충돌의 실제 입력 검사, 지연·손실, Shipping은 별도로 남긴다.

### 적용 및 검증 결과

- 별도 소스 복사본에서 런타임/에디터 모듈의 UHT·컴파일·링크에 성공했다. 최초 시도는 공유 소스 편집 중 생성 헤더와의 불일치가 발생했고, 소스를 복사해 고정한 뒤 해결했다. 중단된 빌드를 이어 완료한 기록은 `Saved/SharedHitStage1/Build-Resumed.log`, 검사 준비값 보완 후 링크 기록은 `Build-Durability.log`다. 전체 Editor 타깃/Shipping 빌드로 확대하지 않는다.
- 표시용 몽타주 2개, Guardian 표시 클립 2개, 일반 피격 공격 자식 BP 2개를 추가했다. Player/Guardian의 수신 프로필과 NPC melee BP 분기 3개를 변경했다. 기본 공격/시작 데이터의 지정과 원본 공격·피격 애니메이션은 유지했다. 원본 7개 파일의 백업과 목록은 `Saved/Backups/SharedHitPresentation/Manifest.json`에 있다.
- 프로젝트의 더 최신 공유 C++ 모듈을 유지했다. PIE를 종료한 뒤 미저장 변경이 없는 대상 BP만 MCP로 편집·컴파일·저장해 열린 에디터에도 같은 분기/프로필을 반영했다. 관련 없는 소스 변경이나 공유 DLL을 이전 복사본으로 덮어쓰지 않았다.
- 현재 프로젝트에서 9개 자산의 재로드/컴파일, Notify·Root Motion 부재, 네 방향/Guardian fallback, 기존 원본 파일 보존을 확인했다. 재로드 검증: `Saved/SharedHitStage1/Verify-ea0bedc3c8b548fea214b30687f8d9af/Result.json` 성공/종료 0.
- 현재 프로젝트의 `ProjectRPG.Combat.Reaction` 자동화 4개가 모두 성공했다(종료 0). 증거: `Saved/SharedHitStage1/Test-a2acadb608364236be439a4893b6f652/Result.json`.
- 현재 프로젝트에서 `Run-SharedHitPresentationTest.ps1 -Port 18860 -TimeoutSeconds 180`을 실행해 서버 역할 프로세스와 원격 클라이언트 2개가 통과했다. 양방향 각각 네 방향, Guardian의 명시적 fallback, 각 클라이언트의 실제 몽타주/섹션과 시퀀스, 승인 신호 재전달의 중복 억제, 무적/치명 타격의 생존 반응 억제를 확인했다. 새 경로에서 이전 HitReact 이벤트가 추가 전송되면 검사에 실패한다. Net/Iris Error·assert·ensure·BP 실행 오류와 완료 후 추가 2초 검사를 통과했다. 증거: `Saved/Tests/SharedHit/be448126a4e743aca1105fff13f872ae/SharedHit.proof.json`과 세 프로세스 로그.
- 위 두 실행의 런타임 DLL SHA-256은 `AA9520E7D918279A259B996CEA29FDE9BFED5EA96FF0A4B3E86880BFF4828332`다. 별도 경로의 앞선 자동화/네트워크 증거는 각각 별도 DLL 해시를 기록하며 현재 프로젝트 실행과 혼용하지 않는다.

첫 별도 네트워크 실행은 Guardian의 원래 HP 70을 여러 방향 검사에 그대로 사용해 두 번째 공격에 사망했고 실패했다(`Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/ddc584b2eb0247038c96b679b305572e/`). 운영 피해나 반응 검사를 완화하지 않고 테스트 월드의 Player/NPC 최대·현재 HP를 10000으로 준비했다. 마지막 치명 타격만 HP 1로 준비한다. 이 값은 개발 플래그가 켜진 검사에만 사용하며 운영 자산의 전투 수치나 밸런스 검증으로 해석하지 않는다.

현재 완료 범위는 **선택형 일반 피격 콘텐츠 연결과 대기 상태의 실제 Iris 전달/네이티브 몽타주 재생**이다. 서버에서 능력을 부여하고 기존 피해 이벤트 처리기를 자극한 검사이므로, Enhanced Input·실제 공격 타이밍·공격/회피와의 충돌·GPU 화면 품질까지 확인한 것으로 간주하지 않는다. 이 확인 뒤 스턴 단계로 넘어간다. 기존 경직 공격은 계속 기존 경로를 사용한다.

## 14. GA 증식 방지를 위한 데이터 전환 — 2026-10-25

### 책임과 정책

GA는 공격 실행·서버 피해 승인을 담당한다. 피격 방식만 달라지는 공격마다 GA 자식 BP를 만들지 않는다. 공격 데이터는 `ERPGAttackHitReactionPolicy`를 선택하고, 피격 대상은 호환되는 애니메이션 프로필을 참조한다.

| 정책 | 승인 피해 이후 동작 |
|---|---|
| `Legacy` (기본값) | 기존 공격의 HitReact 이벤트와 취소·차단 규칙 유지 |
| `None` | 피해는 적용하고 피격 표시와 기존 HitReact 이벤트는 생략 |
| `Cosmetic` | 공통 컴포넌트의 표시만 시도. 대상 프로필이 없거나 재생할 수 없으면 표시를 생략 |

`Cosmetic`의 표시 실패는 기존 경직으로 전환하지 않는다. 새 정책은 피해 성공 여부를 바꾸지 않으며 입력·AI·이동 제한이나 상태이상을 적용하지 않는다. GA와 수신 컴포넌트의 중복 활성화 체크박스를 제거하고, 대상의 `PresentationProfile` 참조로 표시 지원 여부를 정한다.

### 에디터에서 구성하는 위치

| 사용 경로 | 설정 위치와 우선순위 |
|---|---|
| 플레이어 시작 능력/무기 기본·특수 능력 | 기존 `FRPGPlayerAbilitySet` 항목의 `HitReactionPolicy` |
| NPC 시작 능력 | 기존 시작 데이터의 `AttackHitReactionPolicies`: 공격 클래스 → 정책. 가장 가까운 부모 클래스의 설정을 상속하며 정확한 자식 항목이 우선 |
| 공용 SkillContainer | `URPGSkillDefinition.HitReactionPolicy`. 발동 때 만든 RuntimeSpec의 값을 사용하며, 상속된 부여 항목의 정책보다 스킬 정의가 우선 |
| Player/NPC의 표시 애니메이션 | `CombatReactionComponent.PresentationProfile`에 `URPGHitReactionProfile` 데이터 자산 지정 |

NPC는 공통 근접 공격 부모 클래스 한 항목으로 하위 공격군을 구성할 수 있다. 예외 공격만 개별 항목을 둔다. 기존 `NPCStartUpAbilities` 배열은 유지하며, 매핑이 없으면 `Legacy`다. 부모 설정의 범위에는 해당 부모 아래의 모든 능력이 포함되므로 실제 공격 계층에 맞춰 지정한다.

표시 프로필은 방향별 몽타주/섹션, 명시적 fallback과 최대 표시 나이를 가진다. 같은 스켈레톤과 AnimBP/슬롯 구성을 사용하는 캐릭터끼리 재사용한다. Player는 `DA_HitReaction_Player`, Guardian은 `DA_HitReaction_Guardian`을 사용한다. 몬스터 종류마다 GA를 추가할 필요는 없으며, 애니메이션 구성이 다르면 프로필을 추가한다. 기존 몬스터의 공격 몽타주·실행 로직 전체를 범용화한 것은 아니다.

### 구현 및 이행 계약

- 시작/장비 능력은 공통 빌더로 Spec을 만들며 레벨·입력 태그·무기 `SourceObject`를 보존한다. 반응 정책은 서버가 부여한 Spec의 전용 메타데이터 태그에 고정한다. 별도 클라이언트 정책 RPC는 없다.
- 부여 뒤 원본 데이터를 수정해도 기존 Spec의 정책은 바뀌지 않는다. 실행 중 정책은 피해 콜백 전에 읽어 고정하고, SkillContainer는 활성 RuntimeSpec을 사용한다. 입력 조회는 기존 입력 태그만 고르므로 반응 메타데이터가 입력 슬롯이 되지 않는다.
- 무효 정책은 부여/스킬 검증에서 거절한다. 실행 중 Spec이 없거나 손상된 메타데이터가 충돌하면 새 경직으로 되돌리지 않는다.
- 서버 피해 검증·적중 예산·소유권·살아 있는 현재 Pawn/ASC·시퀀스/승인 중복·복구 revision 검사를 유지한다. 기존 NPC BP 분기는 같은 피해 성공 호출에서 결정된 경로를 사용한다.
- 임시 `GA_Player_LightHitAttack`, `GA_Guardian_LightHitAttack`은 참조가 없는지 확인하고 백업 후 제거한다. 기존 부모 공격, 무기/시작 데이터의 공격 지정, 원본 피격 애니메이션과 기본 `Legacy` 동작은 유지한다.
- `Scripts/Editor/ConfigureSharedHitPresentation.py`로 프로필과 대상 참조를 구성하고 `VerifySharedHitPresentation.py`로 별도 프로세스 재로드를 확인한다. 전환 백업/목록은 `Saved/Backups/ReactionPolicyData/Manifest.json`이다.

### 검증 범위

컴파일·자산 전환·집중 자동화·전용 서버 역할 프로세스＋원격 클라이언트 2개의 검증 결과는 아래에 기록한다. 네트워크 검사는 **원래 Player/NPC GA 클래스**를 그대로 부여하고 데이터 정책만 바꿔 실행한다. 검사 전용 HP 10000과 마지막 치명 타격 HP 1을 사용한다. 실제 Enhanced Input, 공격 타이밍, 공격/회피 충돌과 GPU 화면 품질, 지연·손실, Steam/Shipping은 별도 확인 대상이다.

- 물리적으로 복사한 소스에서 런타임·에디터 모듈의 UHT/컴파일/링크가 성공했다(`Saved/ReactionPolicyData/Build-Profile.log`, `Build-Final.log`, 테스트 준비 수정 후 `Build-TestFixture.log`). 모듈 지정 빌드이며 전체 Editor/Shipping 타깃 성공으로 확대하지 않는다.
- 루트 소스 877개와 빌드 복사본의 바이트 일치를 확인했다. 열린 에디터의 PIE를 종료하고 대상 BP 3개와 `testmap`에 미저장 변경이 없음을 확인한 뒤 정상 종료했다. 기존 DLL/PDB는 `Saved/Backups/ReactionPolicyModules-a0e371ffcc334bda8d82e94d2aa00b3b/`에 보존하고 검증 모듈을 적용했다. 이후 `testmap` 에디터를 다시 실행했다.
- 현재 프로젝트의 자산 9개 저장 및 별도 프로세스 재로드/컴파일이 통과했다. 공용 프로필 2개를 추가하고 대상 BP를 연결했으며 임시 GA 2개는 참조 없음 확인/백업 후 제거했다. 원본 공격/피격 애니메이션 4개 파일의 해시는 유지됐다. 구성: `Saved/ReactionPolicyData/Configure-adf8c392e4f048b9839065ef5f830c05/Result.json`, 재로드: `Verify-5749a72a3f7645be9f91099db41ec42c/Result.json`.
- 현재 프로젝트의 피격 5개＋장비 부여 수명/입력·권한 4개＋스킬 정의 2개, 총 11개 native 자동화가 모두 Success/종료 0이었다. 같은 GA의 독립 정책, 부모 공격 설정과 자식 우선순위, 부여 후 데이터 수정 시 고정, 입력 태그·레벨·SourceObject 보존과 스킬 RuntimeSpec 고정을 확인했다. 증거: `Saved/ReactionPolicyData/Test-777887bc2c404c97bc741cafdce1d9df/Result.json`. assert/ensure/fatal은 없었으나 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 남아 전역 오류 0 검증은 아니다.
- 별도 복사 프로젝트의 같은 DLL에서 2인 Iris/양방향·네 방향·fallback·중복·무적·치명 타격 검사가 통과했다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/2bc6193a400d47bb912c8f9f041ffdea/SharedHit.proof.json`.
- 현재 프로젝트에서도 같은 검사를 통과했다(종료 0). 원래 GA 두 클래스를 데이터로 부여한 준비 마커, 각 원격 클라이언트의 0~10 checkpoint와 실제 몽타주/섹션, 새 표시에서 이전 이벤트 미전송, 세 프로세스의 Iris 활성 및 Net/Iris Error·assert·ensure·BP 실행 오류 부재를 확인했다. 완료 후 추가 2초 검사와 DLL 해시 고정도 통과했다. 증거: `Saved/Tests/SharedHit/cd32935337f24bef8e7dfde62cc60236/SharedHit.proof.json`과 세 로그.
- 적용/재로드/native/네트워크 실행의 런타임 DLL SHA-256은 `74DB4DAE207FD8E2E0CAB767BB23C46ADF4529C23CF8642CC853E06BA379B29B`다. 최초 테스트 준비 수정 전 결과와 13절의 구 구현 결과를 이 바이너리의 결과로 혼용하지 않는다.

최초 재로드는 커맨드릿의 네이티브 삭제 반환값이 true여도 GA 파일이 남아 실패했다. 참조/백업을 확인한 해당 파일 두 개만 실제 삭제하고 다음 프로세스에서 부재를 검사하도록 보완했다. 최초 새 정책 테스트는 준비용 `NewObject<UObject>()`가 추상 클래스 ensure를 발생시켜 실패했고, 기존 시작 데이터 객체를 SourceObject로 사용하도록 수정한 뒤 재통과했다. 실패 기록은 각각 `Verify-203d7a65e69e47848ad3795696b3afaa/`, `Test-eb48fac754b3410197064073b7f73ca6/`에 보존한다.

## 15. 입력과 피격 표시의 충돌 검사 — 2026-10-25

### 검사 방법

`Run-SharedHitPresentationTest.ps1 -InputCollisions`는 실제 무기 장비 조정 경로로 기본 공격을 부여하고, 소유 클라이언트의 기존 Enhanced Input 액션/바인딩으로 공격과 구르기를 시작한다. 키보드 하드웨어 입력 대신 액션 주입을 사용하며 `TryActivateAbility`를 직접 호출하지 않는다. 몬스터가 주는 피해는 기존 Guardian 피해 이벤트 처리기에 서버 테스트 자극을 주는 방식이다.

| 순서 | 확인 내용 |
|---|---|
| 공격 → 피격 | 양쪽 클라이언트가 공격을 관측한 뒤 피해 승인. 같은 공격 몽타주 인스턴스 유지와 정상 종료 |
| 피격 표시 → 공격 입력 | 양쪽이 표시를 관측한 뒤, 표시가 실제 재생 중인 소유 클라이언트에서 공격 시작. 표시의 정리와 공격 정상 종료 |
| 구르기 → 피격 | 실제 구르기/서버 이동 승인 중 피해 시도. 무적 규칙에 따른 피해/표시 결과와 같은 구르기 몽타주 유지 |
| 피격 표시 → 구르기 입력 | 표시가 재생 중일 때 구르기 시작. 정상 종료·실제 이동 및 승인 창 정리 |

각 단계는 양쪽 클라이언트의 확인을 기다린다. 취소된 능력, 잘못된 몽타주 교체, 기존 HitReact 이벤트 중복, 서버 이동 검증 위반, 실제 기본 공격의 피해 부재는 실패다. 기존 입력 태그·무기 SourceObject·충돌·서버 피해/이동 허용량을 사용한다. 장비와 정책·HP 10000은 명시적 개발 시나리오의 런타임 준비값이며 원본 콘텐츠나 백엔드를 변경하지 않는다.

`-Rendered`를 함께 지정하면 첫 소유 클라이언트를 GPU로 실행하고 피격 표시·공격·구르기 표본 PNG를 저장한다. 첫 프레임의 대기 포즈를 피하도록 해당 섹션의 0.08초 이후를 관측하고, 검사 카메라에서만 동료 플레이어를 숨기고 모션블러를 끈다. 충돌/서버 판정이나 운영 자산의 카메라·그래픽 설정은 변경하지 않는다. PNG 생성과 네이티브 재생 증거를 구분하고, 화면 내용은 별도로 열어 확인한다. 지연/손실·여러 PC·Steam/Shipping, 모든 타이밍/애니메이션 조합의 품질은 이 검사 범위 밖이다.

### 현재 진행

개발용 단계 검사와 실행 스크립트를 추가했다. 게임 동작/콘텐츠를 변경하는 기능 추가 없이 기존 표시의 입력 충돌을 검증하는 작업이다.

- 별도 복사 프로젝트의 런타임·에디터 모듈 UHT/컴파일/링크가 성공했다. 최종 로그는 `Saved/SharedHitInput/Build-FinalHeaders.log`다. 동시 작업의 새 공유 헤더를 합친 최초 증분 빌드는 생성 헤더 불일치로 실패했고, 강제 헤더 재생성 후 성공했다. 전체 Editor/Shipping 타깃 성공을 뜻하지 않는다.
- NullRHI 2인 입력 검사에서 0~12 단계가 양쪽 모두 확인됐다. 공격 2회와 구르기 2회가 서버/소유 클라이언트에서 취소 없이 종료됐고, 실제 기본 공격 피해와 서버 이동 검증 위반 0건, 구르기 이동/승인 종료를 확인했다. 네 번의 Guardian 피해는 모두 무적 상태가 아닌 대상에 승인돼 HP가 감소했고, 공격/구르기 중에도 같은 몽타주 인스턴스가 유지됐다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/6d5064e1eee4496493cb9197bfc5035c/SharedHit.proof.json`.
- 위 최초 성공 DLL은 `7FAEEEB1754C1ECCEC3665B3C39426CC4F329A6A793B3C10944A3E83E3E54225`다. 공유 헤더 통합 DLL `A7F32A214DBA54556C9D195C56BB07078A214DE1BB38F2291C4689AB89D7B9E0`에서는 기존 양방향·네 방향/fallback·중복·무적·치명 타격의 2인 Iris 회귀 검사가 통과했다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/b42830aef2674a9b8928bd3e3090a798/SharedHit.proof.json`.
- 첫 입력 실행은 구르기 관측 단계에서 시간 초과였다(`e92f49d37042416ea9d3abf6aec45949/`). 새 검사의 구르기 주입 지속 시간/이동 유지 방식을 기존 구르기 검사와 맞추고 최신 설정을 복사한 뒤 재통과했다. 운영 입력·능력 조건이나 이동 허용량을 완화하지 않았다.
- 작업 중 관련 없는 스킬/UI 공유 소스가 계속 갱신돼 검증 복사본을 고정하고 루트 공유 DLL과 열린 에디터를 유지했다. 이번 피격·입력 검사/수신 컴포넌트·플레이어 피해 소스는 루트와 동일하다. 이후 추가된 Player 직업 초기화와 Controller 창 수명 등은 검증 복사본에 미포함이며, 비교한 두 해시/일치 여부는 `Saved/SharedHitInput/FocusedSourceMatch.json`에 기록했다. 다른 작업의 새 구현까지 이 실행으로 검증한 것으로 확대하지 않으며, 해당 변경의 통합 빌드 뒤 같은 러너를 다시 실행한다.
- GPU 소유 클라이언트＋NullRHI 관찰 클라이언트/서버의 입력 검사가 통과했다. 최초 기본 화면 표본은 `f685be01d9974142a20877d245056ddd/`에 있으며, 동료와 모션블러가 동작을 가리는 점을 확인했다. 진단 카메라/캡처 시점을 보완한 DLL의 컴파일/링크는 `Saved/SharedHitInput/Build-Capture.log`에 기록했다. 별도 캐시의 최초 셰이더 컴파일로 지연된 GPU 시도 `86739cb3c13d4e15a0f941bc3a18b1f8/`는 직접 종료했고, GPU 클라이언트가 기존 Zen/DDC를 사용하도록 러너를 보완했다.
- 마지막 GPU/2인 입력 실행은 `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/2f442dc2f7734787902e3b1acfe7c8a3/SharedHit.proof.json`이다. 런타임 DLL SHA-256은 `193312DC968CA4144969C266F09C53E38CA82B7E5BAF28DDA7A56E8817BEDB3B`. 단계 0~12의 양쪽 확인, 취소 없는 공격/구르기 각 2회 종료, 실제 공격 피해·네 번의 승인 피격, 이동 검증 위반 0건과 Iris/Net Error·assert·ensure·BP 실행 오류 부재 및 완료 후 2초 검사를 통과했다.
- 같은 실행의 `Captures/Step-04.png`, `Step-05.png`, `Step-11.png`(1280×720)를 직접 열어 비어 있지 않은 장면/캐릭터·피격/공격/구르기 전환 표본을 확인했다. 정확한 몽타주·섹션·0.08초 이후 재생 위치는 클라이언트의 `LIGHT_INPUT_CAPTURE` 로그로 뒷받침한다. 원본 그래픽 설정의 빠른 구르기에는 모션블러가 크고, 진단 화면도 근접한 NPC/카메라에 따른 일부 가림이 남는다. 따라서 이 결과를 기본 카메라·모든 방향/타이밍의 최종 연출 품질 승인으로 해석하지 않는다. 수동 표본 관측 기록은 `Saved/SharedHitInput/RenderedReview.json`이다.

현재 완료 범위는 **일반 피격 표시와 실제 Enhanced Input 공격/구르기의 양방향 우선순위·수명 검사 및 GPU 표본 관측**이다. 경직·스턴·이동 제약을 추가한 것은 아니다. 다음 기능은 공용 지속 제어 상태의 첫 구현인 스턴이며, 연출 다듬기와 지연/손실 검증은 별도 항목으로 유지한다.

## 16. 공용 스턴 구현 — 2026-10-25

### 구성과 데이터

Player/NPC가 같은 `URPGCombatControlComponent`, `URPGStunEffect`, `URPGCharacterMovementComponent`를 사용한다. 플레이어 전용 이동 보안 컴포넌트는 이 공용 이동 클래스를 상속하고 기존 권한 회수 검사를 유지한다. 표시 전용 피격 컴포넌트와 지속 제어의 수명은 분리한다.

- 기존 플레이어 부여 항목과 `SkillDefinition`에 `StunDuration`(0이면 비활성, 설정 범위 0.05~10초)을 둔다. NPC 시작 데이터는 `AttackStunDurations`의 가장 가까운 부모 공격 설정을 상속하고 자식 항목이 우선한다. 피격 방식만을 위한 GA 자식 BP는 추가하지 않는다.
- 호환 부여 경로는 GAS AbilitySpec의 전용 SetByCaller 메타데이터에 시간을 고정하고, SkillContainer는 발동 시 RuntimeSpec에 고정된 스킬 정의 값을 사용한다. 피해 콜백 전에 설정을 읽는다.
- 스턴은 승인 피해 뒤 네이티브 경로에서만 시도한다. 이미 적용한 피해는 스턴 면역/지원 여부 때문에 되돌리지 않는다. 스턴을 설정한 적중은 이전 경직 이벤트로 대체하지 않는다.
- 대상 기본 면역과 `Shared.Immunity.Control.Stun` 태그를 확인한다. 레이드 보스는 기본 면역이며 BP의 대상 제어 설정으로 명시적으로 변경할 수 있다. 면역 부여는 기존 스턴의 자동 정화가 아니다.

기존 콘텐츠의 값은 0을 유지한다. 스턴 공격을 구성할 때 플레이어 시작 데이터의 `PlayerStartUpAbilitySets`, 무기 데이터의 `DefaultWeaponAbilities`/`SpecialWeaponAbilities` 또는 공용 `SkillDefinition`에서 해당 공격의 `StunDuration`을 지정한다(예: 1.5초). NPC는 시작 데이터의 `AttackStunDurations`에 기존 공격 클래스와 초를 등록한다. 같은 공격 계층 전체에 적용하려면 부모 클래스 한 항목을 사용하고, 자식의 0초 항목으로 예외를 둘 수 있다. 이미 부여/발동한 Spec에는 변경이 소급되지 않으므로 새 부여/발동에서 확인한다.

### 수명과 제어 계약

`Shared.Status.Control.Stunned` 태그는 Duration GE가 소유한다. 정상적인 각 적중은 별도 GE를 부여하고, 겹친 스턴의 실제 종료는 마지막 활성 GE의 만료다. 짧은 재적용이 기존 스턴을 단축하지 않으며 특정 핸들 제거·억제·정화가 다른 출처를 지우지 않는다. 별도 Loose Tag/만료 타이머를 운용하지 않는다.

스턴 진입 시 네트워크 전투 실행을 취소하고 새 발동을 차단한다. 이 범위는 프로젝트 RPG Ability 중 LocalOnly, 부여 시 실행되는 패시브, 사망 실행을 제외한 능력이다. 선택된 실행이 취소 불가능하면 새 스턴을 거절한다. UI/카메라 입력을 일괄 차단하거나 `CancelAllAbilities`를 사용하지 않는다. 이미 생성한 일반 공격 피해 spec도 실제 실행 시 공격자의 스턴을 재검사한다. 독립적으로 존속하는 투사체/향후 DOT의 예외 정책은 해당 기능 단계에서 구분한다.

공용 이동 클래스가 입력 가속·보행 속도·점프·AI 이동 요청·공격 Root Motion의 수평 이동을 제약한다. 서버 `MoveAutonomous`와 소유 클라이언트의 SavedMove 재생이 같은 입력 가속 제약을 따른다. 중력/낙하를 유지하며 이동 모드를 덮거나 종료 때 저장한 속도/`MOVE_Walking`을 복원하지 않는다. 일반 스턴을 던전 퇴장용 RPC 차단이나 전역 이동 보안 승인 취소로 처리하지 않는다.

NPC의 진행 중 이동은 중단하고 프로젝트 AIController의 새 MoveTo와 회전 작업을 거절한다. Brain을 Pause/Resume하지 않으므로 스턴 종료가 사망·전멸·기믹 또는 다른 코드가 멈춘 AI를 재개하지 않는다.

사망/HP 0, ActorInfo 교체/해제, UnPossess, 같은 Pawn 재도전 및 EndPlay에서 모듈 소유 GE만 정리한다. GE Context의 정확한 제어 컴포넌트 참조로 적용 콜백 중의 정화/재도전도 식별한다. 생존·현재 ASC/Avatar·복구 revision을 GE 적용 뒤 다시 확인한다. 기존 HP/사망 원본은 AttributeSet/ASC를 유지한다.

### 클라이언트 표시와 검증

게임플레이 제약은 복제된 GAS 태그를 읽는다. 별도의 `FRPGStunPresentationState`는 최소 GE 복제를 쓰는 NPC도 현재 표시 여부/서버 종료 시각/복구 revision을 알 수 있게 하는 파생 데이터다. 클라이언트의 표시 만료가 서버의 제어를 해제하지 않는다. 공용 Niagara 상태 표시와 상태 변경 델리게이트를 제공하며 지원 애셋이 없어도 제어 수명은 변하지 않는다.

집중 native 검사는 승인 입력/면역·중첩/개별 정화/억제·능력 취소 범위/이동 모드 보존·비취소 실행/콜백 중 재도전/사망/ActorInfo 교체·이미 생성된 공격 피해 거절·부여/스킬 설정 고정을 확인한다. `Run-SharedHitPresentationTest.ps1 -Stun`은 실제 공격 취소→NPC의 승인 피해로 Player 스턴→활성 상태 중 늦은 참가→자연 만료와 입력 이동 회복→Player/NPC 반대 방향 스턴·NPC 이동 명령 거절→중첩/정화·면역/치명 타격을 두 Iris 클라이언트에서 검사한다. `-Rendered`는 상태 표시 PNG를 추가한다. 능력 부여·HP 10000·서버 피해 이벤트/시험 면역은 런타임 준비값이며 운영 자산이나 백엔드 변경은 아니다.

### 확인 결과

- 복사 프로젝트에서 런타임·에디터 모듈의 UHT/컴파일/링크가 성공했다. 소스와 플러그인도 물리적으로 분리했으며, 중복으로 남은 대기 빌드 프로세스를 정리한 뒤 단일 후속 빌드를 확인했다. 마지막 GE 초기화/검사 준비 수정도 각각 빌드했다. 로그: `Saved/CombatStun/Build-IsolatedFinal.log`, `Build-Single.log`, `Build-GEInitialization.log`, `Build-StunProbe.log`. 전체 Editor 타깃이나 Shipping 패키징 성공으로 확대하지 않는다.
- 최종 DLL의 집중 native 17개가 전부 Success/종료 0이었다(스턴 6＋기존 피격 5＋무기 부여 4＋스킬 정의 2). 검사 시작 뒤 Error와 전체 fatal/assert/ensure는 없었다. 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 남는다. 증거: `Saved/CombatStun/Test-79ad373c22b443e3be90aa1798a96d38/Result.json`.
- 같은 DLL의 서버＋원격 2인 Iris 스턴 검사가 통과했다. 스턴 활성 중 늦은 참가, 실제 Enhanced Input 공격 취소·이동/공격 차단, 자연 만료 뒤 이동 회복, Player/NPC 양방향, NPC 새 이동 명령 거절과 정화 뒤 Brain 미재개, 중첩/개별 제거, 면역 중 피해 유지 및 치명 타격 뒤 스턴 부재를 확인했다. 소유 클라이언트의 0~7, 관찰 클라이언트의 1~7 확인과 완료 뒤 추가 2초 검사도 통과했다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/4a1f4111081d48aabe21e87e9dd2bf52/SharedHit.proof.json`.
- 루트와 복사본의 소스 901개, Config 5개, BP 자산 367개, 플러그인 소스와 프로젝트 설정 일치를 확인했다(`Saved/CombatStun/IntegrationInputs.json`). 에디터/빌드 프로세스가 없는 상태에서 이전 모듈을 `Saved/Backups/CombatStunModules-3be7725f4ce542058b40d1db35c87775/`에 보존하고 DLL/PDB를 현재 프로젝트에 적용했다. 적용 기록: `Saved/CombatStun/AppliedModules.json`. 루트의 기존 PDB 두 개는 부재했으며 이를 백업 기록에 명시했다.
- 적용된 현재 프로젝트에서 기존 양방향·네 방향/fallback·중복·무적·치명 타격의 2인 Iris 회귀 검사도 통과했다. 증거: `Saved/Tests/SharedHit/7fd524f7637344c08e8739d00d04479c/SharedHit.proof.json`.

위 최종 런타임 DLL SHA-256은 `F21C8CEE575607BAEF91F06779E01F19CF54E4E9711D63570DC5838663B86739`다. 기동 실패했던 이전 DLL의 결과와 혼용하지 않는다. 최초 실행은 GE 생성자에서 `FindOrAddComponent`가 이름 없는 `NewObject`를 호출해 실패했다. 이름 있는 `CreateDefaultSubobject`와 GE 컴포넌트 등록으로 수정했다(`Test-944f8702ff2a449f8c97d29ad91d94e6/`). 최초 기능 검사는 자연 만료 뒤 전방 NPC 캡슐과 충돌해 이동 회복 거리가 부족했다. 반대 방향 입력으로 검사 준비만 고친 뒤 재통과했다(`6cd02c27cc1b4ca59a93bd42d838eaa6/`).

GPU 실행은 전투 검사를 통과했으나 첫 표본이 Niagara 비동기 컴파일 완료 전에 찍혔다(`Saved/Tests/SharedHit/fbc2637b5ee14943b33091eb91edf746/`). 캡처용 소유 클라이언트에서만 `fx.Niagara.ForceWaitForCompilationOnActivate=1`을 지정한 최종 재실행도 같은 DLL로 통과했다. 증거: `Saved/Tests/SharedHit/3fb62bd08a3e4d7c9f7aa17f13612ebf/SharedHit.proof.json`. 해당 실행의 `Captures/Stun-Active.png`(1280×720)를 직접 열어 캐릭터 머리 위의 밝은 별 모양 스턴 표시를 확인했다. 판정기의 `renderedPixelsVerified=false`는 자동 픽셀 판정을 하지 않았다는 뜻이며 수동 관측은 `Saved/CombatStun/RenderedReview.json`에 별도로 기록한다. 운영 그래픽 설정/콘텐츠나 제어 수명은 이 캡처 설정을 사용하지 않는다. 단일 활성 상태 표본이며 모든 방향·카메라·타이밍과 표시 종료 프레임의 품질 검증을 대신하지 않는다.

실제 네트워크의 스턴 도중 재도전/Pawn 교체, 지연·손실, 여러 PC·Steam·Shipping과 최종 연출 품질은 별도다. 이번 재도전/ActorInfo 교체 검증은 native 수명 검사이며 전체 네트워크 부활 흐름의 검증을 대신하지 않는다. 후속 넉백 구현은 17절을 따르며, 독립 투사체/DOT의 제어 예외와 일반 상태이상은 후속 단계에서 다룬다.

## 17. 공용 넉백 구현 — 2026-10-25

이 절의 별도 검증본/루트 미적용 기록은 당시 결과다. 후속 공유 소스 통합과 현재 프로젝트 적용은 18절에서 완료했다.

### 이번 단계와 데이터

3단계인 **수평 넉백**을 먼저 완결한다. 에어본·다운·기상은 착지 및 전용 애니메이션 계약을 포함하는 4단계이며 이번 완료 범위로 합치지 않는다.

기존 Player/NPC의 `CombatControlComponent`와 공용 이동 클래스를 확장한다. 공격 데이터에 `FRPGKnockbackSettings`(거리·지속 시간)를 두고 시작/무기 부여는 AbilitySpec 메타데이터, 공용 SkillContainer는 활성 RuntimeSpec에 고정한다. NPC의 `AttackKnockbacks`는 가장 가까운 부모 공격 설정을 상속하고 자식의 0cm 항목으로 예외를 둘 수 있다. 기존 콘텐츠는 0cm로 유지한다. 유효 범위는 0~1000cm, 0.1~2초, 최대 2500cm/s다. GA 자식 BP나 체력 컴포넌트는 추가하지 않는다.

예를 들어 450cm/1.2초 넉백 공격은 플레이어 시작 데이터의 `PlayerStartUpAbilitySets`, 무기 데이터의 `DefaultWeaponAbilities`/`SpecialWeaponAbilities` 또는 `SkillDefinition`의 `Knockback.Distance=450`, `Knockback.Duration=1.2`로 구성한다. NPC는 `AttackKnockbacks`에 기존 공격 클래스와 같은 설정을 등록한다. 이미 부여된 Spec/활성 RuntimeSpec의 값은 바뀌지 않으므로 새 부여/발동에서 확인한다. 공격의 일반 피격 정책과 스턴 기간도 별도로 유지한다.

넉백 면역은 대상의 `bKnockbackImmune` 또는 `Shared.Immunity.Control.Knockback` 태그다. 보스의 기본값은 면역이며 대상 BP에서 명시적으로 변경할 수 있다. 면역 중에도 승인 피해는 유지하며 설정된 넉백이 거절됐다고 이전 경직 이벤트를 대신 보내지 않는다.

### 서버 실행과 중단 규칙

- 피해 처리 전에 `FRPGApprovedHitContext`를 스택에 만든다. Source/Target의 현재 ASC·복구 revision·공용 제어 수명 epoch와 충격 방향을 고정하고, 피해 콜백 뒤 같은 생존 수명인지 확인한다. HP 0 전이·ActorInfo 교체·같은 Pawn 재사용은 이전 결과를 무효화한다. 별도 체력 수치나 승인 RPC를 만들지 않는다.
- 새 넉백은 이전 넉백을 교체한다. 각 실행은 증가하는 ID, 자기 Duration GE, RMS 이름/로컬 ID, 이동 승인 GUID와 표시 몽타주 인스턴스만 소유한다. 늦은 종료 요청은 같은 실행 ID가 아니면 거절한다. 스턴 등 다른 상태의 GE는 제거하지 않는다.
- `URPGKnockbackEffect`가 `Shared.Status.Control.Knockback`과 기간을 소유한다. 일반 입력·공격/회피 발동·AI MoveTo/회전은 공통 제어 규칙으로 막고 취소 가능한 전투 Ability만 종료한다. 비취소 전투는 요청 전체를 거절한다.
- 서버에서 고정한 수평 `FRootMotionSource_ConstantForce`를 CharacterMovement에 적용한다. 중력/현재 이동 모드는 유지하고 Z 누적은 제외한다. 스턴과 겹쳐도 이 모듈의 승인된 RMS만 수평 이동 제약의 예외가 된다. 종료 때 `MOVE_Walking`이나 저장했던 속도를 복구하지 않는다.
- 지원 대상은 충돌 질의가 켜진 활성 CharacterMovement의 Walking/NavWalking/Falling이다. Controller가 없으면 엔진의 `bRunPhysicsWithNoController`가 켜져 있거나 SimulatedProxy여야 한다. 물리 구동이 정지한 대상을 넉백 적용 성공으로 처리하지 않는다.
- 앞을 막는 충돌, GE 제거/억제, 다른 이동/Root Motion, 사망·재도전·ActorInfo 교체·EndPlay에서 자기 실행만 끝낸다. 기존 RMS 그룹 전체를 지우지 않는다. 취소한 GAS 몽타주의 Root Motion이 남으면 정확한 이전 인스턴스만 정리하고, 새 몽타주/다른 실행은 건드리지 않는다.
- 플레이어 이동 승인은 해당 실행의 `BeginMovementAuthorization`/`EndMovementAuthorization` GUID로 관리한다. 거리 예산은 설정 거리＋25cm, 창은 설정 기간＋0.2초로 한정하고 실제 종료 때 회수한다. 이미 남아 있는 다른 시스템의 승인 창을 덮어쓰지 않는다. 이동 준비가 거절되면 자기 GE/RMS/승인만 정리하며 이미 처리한 피해나 실행된 취소 콜백은 롤백하지 않는다.

### 네트워크와 표시

`FRPGKnockbackState`는 서버가 확정한 실행 ID·복구 revision·속도·서버 시작 시각·기간·표시 방향을 복제한다. 서버, 소유 클라이언트, 관찰 클라이언트가 같은 이름·힘·기간의 엔진 RMS를 구성한다. CMC가 서버 ID와 로컬 Source를 대응시키고 위치·충돌·예측 보정·SavedMove 재생을 처리한다. 별도 위치 Multicast나 클라이언트 피해 판정은 없다. 늦게 받은 상태는 서버 경과 시간부터 시작하며 만료/다른 수명의 상태는 재생하지 않는다.

교체/종료 시 자기 이름의 Current/SavedRootMotion과 소유 클라이언트 SavedMove의 해당 Source만 제거한다. 표시에는 기존의 안전한 Player/Guardian 피격 프로필을 재사용하며 선택적으로 별도 `KnockbackPresentationProfile`을 지정할 수 있다. 몽타주/섹션·스켈레톤·Notify/Root Motion 안전성을 검사하고 자신의 몽타주 인스턴스만 끝낸다. 이 초기 피격 포즈 재사용을 전용 넉백/에어본 애니메이션의 최종 품질 승인으로 해석하지 않는다.

### 검증 방법

집중 native는 설정/면역/중복, 부여·발동 및 피해 콜백 전 수명·방향 고정, 교체와 다른 RMS 보존, 스턴 중 실제 CMC 이동/중력, 비취소 실행·사망·재도전, 다른 시스템 이동 승인 보존을 확인한다.

`Run-SharedHitPresentationTest.ps1 -Knockback`은 실제 장비 공격 Enhanced Input 취소 → Player 넉백/입력 차단/이동 승인 회수와 두 클라이언트의 종료 위치 수렴 → NPC 넉백 → 서버 벽 충돌/종료 위치 수렴 → 재적중 교체/늦은 종료 무시 → 스턴 중 넉백과 넉백 종료 뒤 스턴 보존 → 면역 중 피해 → 이동 중 사망/정리를 검사한다. HP 10000, 원래 GA 피해 이벤트, 서버 벽 액터는 개발 실행의 준비값이다. 운영 콘텐츠/백엔드는 변경하지 않는다.

지연·손실은 `-PacketLagMinMilliseconds 80 -PacketLagMaxMilliseconds 160 -PacketLossPercent 5`로 별도 실행하고 각 프로세스의 실제 NetDriver 설정도 확인한다. 위치 수렴 기본 허용치는 25cm이며 소유자와 관찰자를 따로 판정한다. 활성 상태의 모든 순간 전송을 보장하는 검사가 아니라 해당 실행의 관측과 최종 정리/수렴을 검사한다.

### 확인 결과

- Source/Plugins를 물리적으로 복사한 별도 프로젝트에서 **전체 Project_RPGEditor Development 타깃**의 UHT/컴파일/링크가 성공했다. 최초 전체 빌드는 `Saved/CombatKnockback/Build.log`, 동시 작업의 성장/로비 소스 통합은 `Build-Integrated.log`, 억제 처리 수정은 `Build-Inhibition.log`, 마지막 검사 코드 수정은 `Build-ProbeTiming.log`다. Shipping/패키징 성공을 뜻하지 않는다.
- 최종 검사 DLL SHA-256은 `D251A37A3B5E74C6706371CA14FA0461A96FE531AF47CCDD88238850CE5CC9DB`다. 집중 native 23개(넉백 6＋스턴 6＋피격 5＋무기 부여 4＋스킬 정의 2)가 모두 Success/종료 0이었다. 적용 콜백 중 즉시 억제된 GE의 시작 거절·진행 중 억제의 즉시 정리·다음 정상 적중도 기존 소유권 검사에 포함했다. 검사 시작 뒤 Error와 전체 fatal/assert/ensure는 없었으며 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 남는다. 증거: `Saved/CombatKnockback/Test-96c076f54604425d8653ff7163aa93a0/Result.json`.
- 최초 native 실행은 Controller가 없는 검사 Pawn의 CMC 물리 구동이 꺼져 있어 실제 이동 항목이 실패했다(`Test-43dade8ecbc8465c9efa278f3ae7ed9d/`). 검사 Pawn에서 엔진의 무컨트롤러 물리 구동을 명시하고, 운영 적용 경로도 구동 가능한 대상인지 검사하도록 보완했다. 이후 native 23개를 재통과했다. 최종 점검에서는 GE 적용 콜백 중 억제될 때 이동이 시작되는 틈도 수정하고 위 native로 확인했다. 이동 보안 허용량이나 서버 권한을 완화하지 않았다.
- 중간 DLL `0F289C638677A411138520801CD8CDF7B05504BB0625CF2B8C22B716AB34334E`의 native 23개와 80~160ms/5% 손실 2인 넉백 검사가 통과했다(`Test-e16af977a8064419b3d4f88f2be53709/Result.json`, `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/5df3dafffb2d4538963a0c0d0a0592b0/SharedHit.proof.json`). 성장/로비 소스를 통합한 `A2337B4684B4B1EC5EB6752BDB34F4DCDEBD0CAF5F59CF3B86909BF41C56EFA4`의 native 23개와 GPU/2인 넉백 검사도 통과했다(`Test-5286ed4b98a842138f708eeeb6038d9a/Result.json`, `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/6ea8148302f24539a1cce13cd074b949/SharedHit.proof.json`). 이 중간 결과를 최종 DLL의 결과로 혼용하지 않는다.
- 억제 수정 뒤 `45323D0A3B9041F96B0E2E0E4624BD20F5D6E33C58B4B7E36B78A8FCCDFFDEBE`의 기존 2인 Iris 스턴 회귀 검사가 통과했다. 활성 스턴 중 늦은 참가·자연 만료·입력 회복·양방향 스턴·중첩/정화·면역/치명 타격을 포함한다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/79ff476b9dcc45cabef51cebe74c1f48/SharedHit.proof.json`. 이후 최종 검사 DLL까지 달라진 소스는 개발용 `RPGKnockbackNetworkProbe.cpp` 하나이며 게임플레이 소스는 같다(`Saved/CombatKnockback/FinalProbeSnapshot.json`).
- 최종 DLL의 **GPU 소유 클라이언트＋NullRHI 관찰 클라이언트/서버, 각 NetDriver 80~160ms 지연·5% 손실** 검사도 통과했다. 양쪽의 0~14 단계, 실제 Enhanced Input 공격 취소/이동 차단, Player/NPC 양방향, 종료 위치 수렴(25cm), 벽 충돌·교체·스턴 중첩·면역·활성 넉백 중 사망 정리와 이동 보안 위반 0건을 확인했다. 세 로그의 실제 패킷 설정·Iris 활성·Net/Iris Error/fatal/assert/ensure/BP 실행 오류 부재·완료 뒤 2초 검사 및 DLL 해시 고정도 통과했다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/cf555261a59a46048baeca252ff9bcc4/SharedHit.proof.json`.
- 위 실행과 중간 GPU 실행의 `Captures/Knockback-Active.png`(1280×720)를 직접 열었다. 캐릭터와 피격 포즈가 표시된 장면을 확인했으며 최종 표본에는 가까운 NPC가 머리/상체 일부와 겹친다. 정지 표본은 이동 궤적을 증명하지 않으며 실제 이동/수렴은 별도 네트워크 로그로 판정한다. 자동 픽셀 판정이나 전용 넉백 애니메이션의 품질 승인은 하지 않는다. 수동 관측 기록: `Saved/CombatKnockback/RenderedReview.json`.

루트 소스에 구현을 반영했고 검증은 물리적으로 분리한 복사 프로젝트에서 실행했다. 검증 복사본과 루트의 전체/집중 소스 차이 및 해시는 `Saved/CombatKnockback/FinalSourceMatch.json`, `FinalProbeSnapshot.json`, `FinalInputProvenance.json`에 보존한다. 작업 중 다른 성장/로비 소스가 계속 갱신되고 루트 빌드와 에디터도 실행돼 **이 작업에서는 루트 DLL을 교체하지 않았다**. 현재 열린 에디터의 동작까지 최종 DLL의 결과로 간주하지 않는다. 통합 증거는 `Saved/CombatKnockback/Verification.proof.json`이다.

마지막 비교에서는 대검 Resolve/아이덴티티와 로비의 후속 변경이 추가됐다. Player 피해 파일, SkillContainer cpp/h도 달라져 전체/집중 파일의 바이트 일치를 주장하지 않는다. Player 피해 처리·SkillContainer 반응 라우팅·공용 승인 후 라우팅 함수가 검증본과 같고, 피해 콜백 전 컨텍스트 고정/결과 배열 연결이 유지된 것은 확인했다(`ConcurrentOverlapReview.json`). 이 코드 보존 검토를 최신 아이덴티티 콜백·피해 배율·로비/기본 맵 설정까지 합친 빌드/실행 검증으로 확대하지 않는다. **최신 공유 소스의 통합 빌드와 현재 프로젝트 재검증을 먼저 완료한 뒤 에어본 단계로 진행한다.**

첫 GPU＋80~160ms/5% 손실 실행은 13단계에서 관찰자의 활성 넉백은 확인됐지만 소유자 확인이 오지 않아 직접 종료했다(`be2bda2e1c864f029756e9d25388e09a/FailureReview.json`). 당시 기록만으로 단발 관측 누락의 원인을 확정하지 않는다. GE 태그/RMS/몽타주/서버 경과 시간 진단을 추가한 동일 조건 재실행 `6fc3d5348d424e33a170669f310c6fc1/SharedHit.proof.json`은 전 단계를 통과했다(DLL `EC00F07AC45D8C1B3D35FD36B5BD540C068803AD9E4578B40586BB8C7A1A08F9`). 이후 검사에만 필요한 0.3초 입력 유지 대기를 첫 입력 차단 단계로 한정하고, 사망 검사 준비 중 넉백이 만료되면 즉시 실패하도록 보완했다. 게임플레이 지속시간·복제 방식·보안 예산은 변경하지 않았다. 이 기록을 모든 실행/모든 순간 상태 관측의 보장으로 확대하지 않는다.

현재 검사의 두 번째 클라이언트는 넉백 시작 전에 참가한다. **활성 넉백 도중의 늦은 참가/재관련성**, 네트워크 재도전·Pawn 교체, 여러 PC·Steam/Shipping과 모든 방향/타이밍의 최종 연출 품질은 별도다. 재도전/ActorInfo 교체는 native 수명 검사이며 실제 네트워크 부활 흐름을 대신하지 않는다. 에어본·다운·기상은 다음 구현 단계다.

## 18. 최신 공유 소스 통합과 현재 프로젝트 검증 — 2026-10-26

### 통합 방법

17절 뒤 갱신된 대검 Resolve/아이덴티티, SkillContainer 피해 배율·확정 적중 보상, 실행 정책, 로비/기본 맵 설정을 현재 소스와 함께 통합했다. `FRPGApprovedHitContext`의 기본 생성자 추가도 포함한다. 피해 전 컨텍스트 고정과 피해/보상 콜백 뒤 같은 수명인지 검사하는 경로는 유지한다.

1. Source 920개와 Config·BP·플러그인 소스/설정·프로젝트 파일, 최초 1567개 입력을 물리적으로 분리한 기존 빌드 프로젝트에 맞추고 해시를 기록했다. 검증 중 저장된 최신 `BP_PlayerController`도 반영했다. 마지막 비교에서 확인한 파티 UI BP 1개를 보존해 복사본에 맞춘 최종 입력은 1568개다. 원본/검증 입력 목록은 `Saved/CombatKnockbackIntegration/InputSnapshot.json`을 따른다.
2. 전체 `Project_RPGEditor Win64 Development` 타깃을 강제 UHT 재생성으로 빌드했다. 최초 통합 164개 작업과 이후 검사 cpp 수정의 증분 컴파일/링크가 성공했다. 로그는 같은 폴더의 `Build.log`, `Build-Fixture*.log`, `Build-Acknowledgement.log`다. Shipping/패키징 결과는 아니다.
3. 분리 프로젝트에서 집중 검사를 먼저 통과시킨 뒤, 프로젝트 에디터/빌드가 없는 상태에서 기존 프로젝트·플러그인 DLL/PDB를 백업하고 같은 입력으로 빌드한 모듈을 적용했다. 엔진 BuildId 일치도 확인했다. 최초 34개 파일, 검사 완료 확인 수정 뒤 런타임 DLL/PDB 2개를 추가 적용했다. 적용·이전 해시·부재했던 파일과 백업 위치는 `AppliedModules-Initial.json`, `AppliedModules.json`에 기록한다. 콘텐츠/기본 맵을 이전 상태로 되돌리지 않는다.
4. 적용한 현재 프로젝트에서 같은 native 검사와 2인 네트워크 러너를 실행한다. 분리 프로젝트 결과를 현재 프로젝트 실행으로 대신하지 않는다. 최종 입력·모듈 일치와 실행 결과는 `Verification.proof.json`에 모은다.

### 기존 검사 준비와 완료 확인 보완

집중 native는 기존 23개에 공유 SkillContainer 관련 피해 보안 15개, RuntimeSpec 6개, 실행 설정 1개, 스킬 통합 7개, 로비 판단 2개를 더한 54개다. 새 검사 클래스를 추가한 것은 아니다.

기존 스킬 통합 준비는 직업 소유·습득·장착 슬롯 없이 능력을 직접 부여하고 있었고, 현재 직업 권한 검사에 거절됐다. `RPGSkillRuntimeSpecTests.cpp`의 공용 준비를 네이티브 Player의 신뢰된 복구 → 직업 소유 Spec 부여 → 슬롯 복구 → 슬롯/ASC 입력 경로로 맞췄다. 임시 직업/스킬/SP는 검사 월드에서만 사용한다. 임시 정의와 CDO 연결은 준비 종료 시 복원하며 실제 에셋이나 서버 권한 검사를 수정하지 않는다. OnRep 준비 데이터에는 현재 복구 세대도 넣는다.

트라이포드 검사는 현재 계약인 **실행 중 설정/레벨 변경 거절**, 종료 후 변경·다음 발동 반영, UI 재클릭의 선택 해제/재선택과 명시적 중복 명령의 무변경, 서버 OnRep 복구를 확인하도록 기존 기대를 보완했다. 최초 및 중간 실패 결과도 `Test-f2ddb0e154fc4ffca702c25778077f97`, `Test-5e825f01435c41ffb02cf0716d1cacf9`, `Test-cd75cd5060c642198daca97d63a8b835`, `Test-03fe18330d4840449f7b467acef70dd9`, `Test-74885defcb154415bec49ae661e10386`에 보존한다.

현재 프로젝트의 최초 지연/GPU 넉백 실행 `Saved/Tests/SharedHit/7d914f9dc2464524a2cdb2e92b664ca1/`은 1단계 종료 확인에서 실패했다. 양쪽의 활성 관측 로그가 있으나 기존 판정은 GE 만료 순간에 확인 RPC도 서버에 도착해 있어야 했다. 개발용 검사에서 자연 종료 후 이미 관측한 결과의 확인 RPC만 최대 2초 기다리도록 분리했다. 클라이언트의 활성 GE/RMS/포즈 관측, 거리/정리·이동 승인·보안 판정은 유지하고, 첫 입력 차단 검사가 끝나면 주입 입력을 즉시 해제한다. 사망 검사는 계속 **활성 넉백 중**에만 진행한다. 게임플레이 기간·이동 예산·복제 방식은 바꾸지 않았다.

### 확인 결과

- 최종 런타임 DLL SHA-256은 `BB5A5642C0135EB7B5D1F9CB0F039B26F49EEE6511A22E77ED9209A7F676C49D`다. 분리 프로젝트와 **적용한 현재 프로젝트의 native 54개가 모두 Success/종료 0**이었다. 증거: `Saved/CombatKnockbackIntegration/Test-f4ecf4188ffc45e59b18ca38d0c8079b/Result.json`, `Test-fbc750030b524329ab8d4321c20908b8/Result.json`.
- 같은 현재 프로젝트 DLL의 GPU 소유자＋NullRHI 서버/관찰자 2인 Iris 넉백 검사도 통과했다. 각 NetDriver의 80~160ms/5% 손실 설정, 양쪽 0~14 단계, 이동 수렴·벽 충돌·교체·스턴 중첩·면역·활성 이동 중 사망 정리, 이동 검증 위반 0건 및 완료 뒤 2초를 확인했다. 증거: `Saved/Tests/SharedHit/858c611481dd4e48839ccc7aa07aaa57/SharedHit.proof.json`.
- 같은 현재 프로젝트 DLL로 기존 2인 Iris 스턴과 일반 피격 회귀도 통과했다. 스턴은 활성 중 늦은 참가·자연 만료·입력 회복·양방향·중첩/정화·면역/치명 타격, 일반 피격은 네 방향/fallback·중복·무적·치명 타격을 포함한다. 증거: `Saved/Tests/SharedHit/3afdbac9883e4f0eb0555b302f867d66/SharedHit.proof.json`, `Saved/Tests/SharedHit/61bab0b618b74f24a78b3f686bfaa8b0/SharedHit.proof.json`.
- 최종 native 두 실행의 검사 단계 Error는 0이며 fatal/assert/ensure도 없었다. 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 남는다. 각 네트워크 실행은 Net/Iris Error·assert/ensure·BP 실행 오류 부재와 완료 뒤 2초 및 DLL 해시 고정을 확인했다.
- 현재 프로젝트의 `Knockback-Active.png`(1280×720)를 직접 열어 비어 있지 않은 장면과 가려지지 않은 플레이어 전체/피격 자세 표본을 확인했다. 실제 이동/수렴은 로그로 검증하며 이 한 장을 궤적이나 최종 애니메이션 품질의 증거로 삼지 않는다. 수동 관측은 `Saved/CombatKnockbackIntegration/RenderedReview.json`에 기록한다.
- 검증 뒤 현재 프로젝트의 `testmap` 에디터를 다시 실행했고 현재 DLL 로드를 확인했다. 재실행 프로세스·기동 로그는 `ReopenedEditorProcess.json`, `ReopenedEditor.log`, `ReopenedEditorSmoke.json`에 기록한다. 최종 입력/바이너리 일치는 `IntegrationInputs.json`, 결과 모음은 `Verification.proof.json`을 따른다(모두 `Saved/CombatKnockbackIntegration/`).
- 추가된 `WBP_DungeonPartyCombat`은 최종 현재 프로젝트 검사 전에 저장된 파일이었다. 루트 자산을 변경하지 않고 별도 프로세스에서 로드·컴파일 상태 `BS_UP_TO_DATE`와 파일 해시 보존을 확인했다. 증거: `AdditionalPartyWidget.json`, `PartyWidgetReload.json`, `PartyWidgetReload.log`(같은 통합 폴더). 전체 파티 UI 조작 검증을 뜻하지 않는다.

이 통합 확인은 대검 아이덴티티의 모든 전투 조합, 로비 HTTP/백엔드 전체 흐름, 넉백 도중 늦은 참가·실제 네트워크 재도전/Pawn 교체, 여러 PC·Steam/Shipping과 최종 연출 품질을 대신하지 않는다. 다음 기능은 4단계 에어본·다운·기상이며, 공중/착지 판정과 대상별 애니메이션 계약부터 확정한다.

## 19. 공용 에어본·실제 착지·다운·자동 기상 — 2026-10-26

### 구현 방법

- 기존 `FRPGKnockbackSettings`에 `Height`, `DownDuration`, `GetUpDuration`을 더한다. 높이 0은 기존 수평 넉백, 50~600cm는 에어본이며 거리 0인 수직 에어본도 지원한다. GA 클래스·무기 부여·NPC 부모 클래스별 공격 설정·SkillDefinition/RuntimeSpec은 같은 구조를 사용한다. 몬스터마다 GA를 추가하지 않는다. Spec 부여/활성화에서 설정을 고정하고 부분적으로 누락된 메타데이터는 거절한다.
- 기존 `URPGCombatControlComponent`가 Player/NPC 모두의 실행을 소유한다. HP·사망은 기존 AttributeSet/ASC를 사용한다. `FRootMotionSource_JumpForce` 하나로 이동하며 LaunchCharacter를 섞지 않는다. 실제 중력으로 `sqrt(8 * Height / -GravityZ)`의 비행 시간을 계산하고 CMC 충돌·중력·이동 복제를 유지한다. 임의 중력 방향과 비정상 속도·시간은 거절한다.
- 전체 실행의 무기한 `KnockdownSequence` GE로 행동 차단을 유지하고, Airborne → Downed → GettingUp 단계 GE를 별도로 교체한다. 공중 단계는 실제 서버 `LandedDelegate`의 보행 가능한 충돌만으로 다운에 진입한다. 다운·기상 지속시간과 자연 만료는 GAS가 관리한다. 중간 단계 해제/억제는 자기 실행 전체를 정리한다. 자동 기상만 구현하며 임의의 기상 무적·쿨다운·입력을 만들지 않는다.
- 벽·천장 충돌은 자신의 이동 소스만 중단하고 자연 낙하/실제 착지를 기다린다. 비행 시간 + 2초 내 착지하지 못하면 다운을 만들지 않고 실행을 종료한다. 바닥을 잃은 다운·기상도 정리하며 종료 시 Walking을 강제 복원하지 않는다.
- 실행 ID·생명 revision·phase revision, 서버 시각과 고정된 궤적 값을 복제한다. 서버/자율 프록시/관찰 프록시가 같은 이름·설정의 RMS를 연결하며, 오래된 단계는 재생하지 않는다. 종료/교체 시 자기 RMS와 예측 SavedMove의 같은 소스, 자기 몽타주의 정확한 인스턴스만 제거한다.
- 공중 비행에만 GUID 소유 이동 보안 승인을 부여한다. 시간은 최대 비행 + 여유, 3D 이동 거리 예산은 수평 거리 + 높이 왕복 + 여유로 제한한다. 실제 착지·중단·사망·재사용 시 회수하며 다른 시스템의 승인을 덮지 않는다. 독립 스턴은 보존한다. 에어본은 수평 넉백보다 우선하고 독립 면역 태그를 사용한다. 레이드 보스는 기본 면역이다.
- 프레젠테이션은 스켈레톤별 `URPGAirbornePresentationProfile`의 세 몽타주로 교체한다. 공중·다운은 루프, 기상은 서버 경과 시간으로 맞춘 단발이다. Notify/애니메이션 Root Motion이 있는 클립은 거절한다. 원본을 수정하지 않고 안전한 복사본을 만든다. Player는 호환되는 낙하·다운 반복·기상 클립, Guardian은 호환되는 낙하·웅크림·회복 클립을 사용한다. **Guardian 웅크림은 임시 아트이며 전용 엎어진 다운/기상 애니메이션 완료로 간주하지 않는다.**

### 검증과 적용

초기 구현을 현재 프로젝트에 적용하고 아래 범위를 검증했다. 기존 공격의 기본 거리·높이는 0이며 공격 데이터에서 명시적으로 켠다. 예를 들어 기존 NPC 공격 클래스의 `AttackKnockbacks`에 Distance=300, Height=200, DownDuration=0.8, GetUpDuration=1.2를 설정한다. Height가 양수면 Duration 대신 실제 중력으로 비행 시간을 계산한다. Player 무기/시작 부여 데이터와 SkillDefinition의 같은 구조도 사용한다.

- 최신 로비·캐릭터 생성·스킬 변경을 보존해 분리 프로젝트의 물리 복사본에서 전체 Editor Development 타깃을 빌드했다. 새 cpp는 UBT 파일 목록을 다시 수집해 포함했다. 빌드 입력/적용 목록은 `Saved/CombatAirborne/BundlePlan.json`이다. 최종 DLL SHA-256은 `3BDACF418A2BF615E535B1FAF5C8AECECAE48B5F611F5167A0A828EDAAECFDFC`다.
- 분리 프로젝트와 **적용 후 현재 프로젝트에서 native 60개가 전부 Success/종료 0**이다. 새 에어본 6개와 기존 전투·부여·스킬·피해 보안·로비 판단 54개를 포함한다. 설정 고정·유효성, 실제 바닥 착지/다운/자동 기상, 면역·중복·교체·정화·억제, 벽·천장·무바닥 시간 초과, 사망·재사용·재진입·바닥 상실, 외부 RMS/보안 승인 보존을 검사했다. 증거: `Test-a3d3b7eecbdb400db3d3bb916196b4d3/Result.json`, `Test-72e219fc02e542d3a7dcca6aebd995bf/Result.json`(모두 `Saved/CombatAirborne/`).
- 첫 새 검사 준비는 같은 엔진 프레임을 반복 사용해 물리가 진행되지 않았다. 프레임을 스코프에서 진행하고 복원하도록 수정했다. 최신 공용 스킬 조회의 유효 SkillTag 조건에 맞춰 기존 피해 검사 Runtime 준비에 실제 스킬 태그를 넣었다. 실제 조회·권한·피해 승인 규칙은 완화하지 않았다. 초기 실패 기록도 보존했다.
- 모듈 DLL/PDB 20개와 안전한 애니메이션 복사본·프로필·대상 BP 16개를 백업 후 적용했다. 원본 애니메이션 해시와 엔진 BuildId를 확인했다. 백업: `Saved/Backups/CombatAirborne-c319a477ea0b4450aa7a49cbb32d0409`. 적용 목록: `Saved/CombatAirborne/AppliedBundle.json`.
- 같은 최종 DLL의 **현재 프로젝트 2인 Iris 검증**은 GPU 소유자 + NullRHI 관찰자/서버, 각 NetDriver 80~160ms/5% 손실에서 통과했다. 실제 Player/NPC 피해 승인 핸들러를 양방향으로 자극하고, 양쪽 클라이언트의 0~9단계, 활성 RMS와 실제 공중 높이, 실제 착지·다운·기상 GE/몽타주, 입력 차단, 기상 후 위치 수렴과 독립 면역을 확인했다. 이동 보안 위반 0건이며 완료 후 2초 동안 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없었다. 증거: `Saved/Tests/SharedHit/58752454abf44365b899972ed57a8512/SharedHit.proof.json`. 선행 분리 빌드 검증은 `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/65a20ff2e3c44e7db77c62f01e6754ea/`에 있다.
- 현재 프로젝트의 Phase1/2/3 PNG를 직접 열어 플레이어의 공중·누운 다운·기상 도중 자세를 확인했다. 표본 이미지와 몽타주 관측은 최종 연출 품질 승인이나 궤적 전체를 대체하지 않는다. 원본 클립 보존/안전 프로필 생성·재로드 스크립트는 `Scripts/Editor/ConfigureSharedAirbornePresentation.py`, `VerifySharedAirbornePresentation.py`다.
- 에디터 종료 확인 스크립트의 실패가 다음 PowerShell 종료 요청을 차단하지 못해 **기존 PIE와 에디터가 일반 닫기로 종료되는 실수**가 있었다. 에디터 강제 종료나 저장 취소는 수행하지 않았다. 적용 후 `bossmap` 에디터의 정상 로드와 응답을 확인했으며 이전 PIE 플레이 상태는 복원하지 못했다. 상태/기동 기록: `Saved/CombatAirborne/ReopenedEditorProcess.json`, `ReopenedEditor.log`.

최초 적용한 DLL은 `7149C66CC750027B59DDADC149F870730138BE4F70D471758BDF2690D1056D69`였다. 이후 현재 프로젝트가 다른 빌드로 갱신되어 해당 파일을 보존하고, 위 최종 DLL의 native 60개와 2인 Iris/GPU 검증을 다시 통과시켰다. 최종 비교에서 Source 931개는 적용 시점의 검증 입력과 일치했으며, 적용한 에셋 16개도 일치했다. 두 빌드의 파일 해시와 최초 적용/후속 재빌드 구분을 결과 모음에 남겼다.

최종 결과 모음은 `Saved/CombatAirborne/Verification.proof.json`이다. 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 남으며 실제 테스트 단계의 Error와 구분한다. 에어본 진행 중 늦은 참가, 실제 네트워크 재접속/Pawn 교체, 경사·낭떠러지의 복잡한 조합, 다른 이동 능력/보스 패턴 전체, 여러 PC·Steam·Shipping, 전용 Guardian 다운/기상 아트는 추가 작업이다. 후속 기능은 지속 상태이상이다.

## 20. 공용 둔화와 이동 속성 소비 — 2026-10-26

### 구현 방법과 정책

- 계획의 5단계 순서에 따라 둔화부터 연결한다. Player/NPC 부여 데이터와 SkillDefinition/RuntimeSpec에서 같은 `FRPGSlowSettings`를 사용한다. Percent=0은 비활성, 5~90%와 0.05~30초를 지원한다. NPC는 `AttackSlows`의 가장 가까운 부모 클래스 설정을 상속하며 자식의 0% 설정으로 끌 수 있다. GA 자식 BP를 추가하지 않는다. 설정은 부여/활성화 때 고정하고 피해 콜백 전에 보관한다.
- 기존 승인 피해가 성공하고 같은 생명·Pawn/ASC·세계·제어 epoch가 유지될 때만 공용 CombatControlComponent가 Native Duration GE를 적용한다. 피해 없는 상태 적용 RPC/BP API는 추가하지 않는다. 둔화는 공격·점프·AI·회전을 취소/차단하지 않으며 기존 공격의 피격 표시 정책을 유지한다. 스턴·강제 이동과는 별개의 효과다.
- 실제 Player 시작 GE를 조사한 결과 `MoveSpeed`는 Instant Override 1이었다. 이를 기존 cm/s 설정의 배율로 확정한다. AttributeSet 기본값도 1로 맞추어 별도 초기 GE가 없는 NPC의 300cm/s를 보존한다. 공용 CMC의 걷기/NavWalking/공중 자율 이동 속도는 `현재 기본 속도 × MoveSpeed × max(0, 1 + MoveSpeedPercent/100)`이다. 타깃 락의 기본 속도 제한은 배율 계산 전에 적용한다. 수영·비행과 직접 RMS/애니메이션 Root Motion은 이번 둔화 대상이 아니다.
- 둔화 GE는 MoveSpeed에 `(1 - Percent/100)`의 Multiplicative modifier를 부여한다. MoveSpeed aggregator에는 엔진의 `MostNegativeMod_AllPositiveMods`를 사용해 가장 강한 감소와 양수 modifier를 유지한다. 같은 속성의 modifier 합성은 GAS 규칙을 따르며 별도 수동 곱셈/중첩 저장소를 만들지 않는다. 각 적중은 독립 GE 기간을 갖고 강한 효과가 만료/정화/억제되면 남은 약한 효과로 자동 복귀한다. 한 대상의 공용 런타임 소유 효과는 최대 32개로 제한한다.
- 면역은 `Shared.Immunity.Movement.Slow`와 대상 기본값으로 관리하며 피해 승인과 독립이다. 보스는 기본 둔화 면역, Player/일반 NPC는 기본 허용이다. 부여가 끝난 둔화는 공격자의 사망/제거 뒤에도 대상 GE 만료까지 남는다. 대상 사망·권한 회수·Avatar 재사용/교체·EndPlay는 이 수신기가 만든 GE만 제거한다. 외부 버프·스턴·이동 승인이나 이동 모드를 복원/해제하지 않는다.
- 타깃 락은 과거 MaxWalkSpeed를 저장/복구하는 대신 공용 CMC의 GUID 속도 제한을 소유한다. 해제 시 자기 GUID만 제거하므로 그동안 바뀐 기본 속도·장비/버프·둔화를 덮지 않는다. 기존 로컬 타깃 탐색/UI/입력 정책은 유지한다.
- NPC Minimal 복제에서도 이동 배율은 기존 AttributeSet 복제를 소비한다. 별도 복제 `SlowState`는 가장 강한 둔화율·종료 시각·생명 revision의 읽기 전용 표시다. 공용 비상호작용 머리 위 위젯에 둔화율/남은 시간을 표시하며 `bShowSlowStatus`로 끌 수 있다. GE가 기간·집계·정화의 원본이고 스냅샷이나 UI 시계가 속도를 결정하지 않는다.
- RuntimeSpec Reset은 이전 활성화의 피격 정책·스턴·변위·둔화를 초기화해 다음 스킬에 부가 상태가 남지 않게 한다.

### 검증과 적용

초기 구현을 현재 프로젝트에 적용하고 검증했다. 기본 공격 데이터의 Percent는 0으로 유지했다. 사용하려면 기존 NPC 공격 클래스의 `AttackSlows`에 Percent=40, Duration=3을 넣거나 Player 무기/부여 데이터·SkillDefinition의 `Slow`를 설정한다. 보스는 대상 CombatControlComponent의 기본 면역을 명시적으로 해제해야 한다. 상태 표시가 필요 없으면 `bShowSlowStatus`를 끈다.

- 전체 `Project_RPGEditor Win64 Development` 빌드가 통과했다. 최종 DLL SHA-256은 `2CC7545EE846ED0470331D15AF1C6DDD37B20B97AD18765CF506DC361FBCFB95`다. Source/설정/BP/플러그인 입력 1603개를 맞추고, 엔진 BuildId 일치를 확인한 DLL/PDB 34개를 백업 후 적용했다. 콘텐츠 BP나 원본 애니메이션 수정은 없다. 목록/백업은 `Saved/CombatSlow/BundlePlan.json`, `AppliedBundle.json`, `Saved/Backups/CombatSlow-89614274a9d94b8faae8fb94d4a40d20`에 있다.
- **같은 DLL의 분리 프로젝트 및 적용 후 현재 프로젝트 native 65개가 모두 Success/종료 0**이다. 새 둔화 5개와 기존 스턴·넉백·에어본·피격·부여·스킬·피해 보안·로비 판단 60개를 포함한다. 설정 고정/유효성·활성화 Reset, 실제 CMC 속도와 거리, 가장 강한 효과 만료 뒤 약한 효과 복귀, 외부 버프·현재 기본 속도와 GUID 제한 보존, 면역/중복/정화/억제·사망/재사용/재진입·공격자 제거 후 자연 만료를 확인했다. 증거: `Saved/CombatSlow/Test-c1d6732e21834fec8b9c9591542f3b4e/Result.json`, `Test-cd746026f5bf41b0a70de6fad45242d0/Result.json`.
- **현재 프로젝트 2인 Iris 검증**은 GPU 소유자 + NullRHI 관찰자/서버, 각 NetDriver의 80~160ms/5% 손실에서 통과했다. 실제 Player/NPC 피해 승인 핸들러 양방향, 장비로 부여한 공격의 취소 없음, Enhanced Input 이동과 현재 속도, 50%→75%→50%→정화, NPC의 50% 실제 이동/자연 만료, 면역·치명타격 정리, 양쪽 상태 UI 생성/종료를 확인했다. 첫 클라이언트의 활성 둔화 중 두 번째 클라이언트를 접속시켜 현재 배율·남은 시간·상태 표시의 복원도 확인했다. 각 클라이언트의 0~10/1~10단계와 완료 후 2초가 통과했고 이동 보안 위반은 0건이다. 증거: `Saved/Tests/SharedHit/3740a51013d74d2aa9a2733ca43b9276/SharedHit.proof.json`.
- 선행 분리 프로젝트의 같은 2인 실행은 `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/58bf601dfd214734a486a5e1402f44b8/SharedHit.proof.json`이다. 늦은 참가 준비 중에는 실제 승인 피해로 30초짜리 GE를 필요한 경우 다시 적용한다. 실제 게임의 재적용이나 UI 기간을 무한으로 바꾼 것은 아니다. 테스트의 피해 사건 주입/직접 NPC Spec 부여·내구도 10000·Brain 정지 준비를 자연 전투 전체나 백엔드/인벤토리 연결 검증으로 해석하지 않는다.
- 초기 네트워크 이동 검사는 앞에 배치한 공격용 NPC 캡슐과 충돌해 진행되지 않았다. 실제 피해 승인 후 해당 검사 NPC를 이동 경로 옆으로 옮겼고 이동/속도 판정은 유지했다. 늦은 참가자의 시작 단계가 1이므로 그 첫 관측에서도 NetDriver 설정을 기록하도록 검사 로그를 보완했다. 초기 실패/중단 기록은 보존했다. 실게임 충돌·이동 보안 정책은 완화하지 않았다.
- 현재 프로젝트의 `Slow-Step1.png`와 `Slow-Step6.png`를 직접 열어 공용 둔화율/남은 시간 표시를 확인했다. 화면 표본은 최종 HUD/이펙트 품질 승인이나 물리 키보드 검증을 대체하지 않는다. `Saved/CombatSlow/RenderedReview.json`에 기록했다.
- 이번 적용 전 기존 에디터의 전체 기동 로그에 PIE 실행 기록이 없음을 확인했다. 일반 닫기 후 실제 정상 종료를 확인하고 모듈을 적용했으며, 강제 종료 없이 `bossmap` 에디터를 재실행해 로드/응답을 확인했다. `EditorBeforeClose.json`, `ReopenedEditorProcess.json`, `ReopenedEditor.log`(모두 `Saved/CombatSlow/`)에 남겼다.

최종 결과는 `Saved/CombatSlow/Verification.proof.json`이다. 실제 테스트 단계 Error/Fatal/Assert/Ensure는 0이고 기존 기동 `Condition failed` 15줄은 별도다. 타깃 락의 전체 UI 활성화, 모든 장비/버프 조합, 다른 이동 모드, 네트워크 재접속·실제 Pawn 교체, 4인·여러 PC·Steam/Shipping과 최종 상태 연출은 별도 검증이다. 다음 기능은 지속 피해 한 종류이며 출처 수명·귀속·회차 예산과 정화 규칙부터 확정한다.

## 21. 공용 면역 분류와 실행 단위 스킬 면역 — 2026-10-26

### 21.1 범위와 판정

`FRPGCombatImmunitySettings`의 `Flags`를 Player 부여 데이터, NPC 시작 데이터의 `AbilityImmunities`, SkillDefinition의 `ActivationImmunities`에서 설정한다. 같은 GA에 데이터를 다르게 부여할 수 있고, NPC는 가장 가까운 부모 클래스 설정을 사용한다. 0을 명시하면 해당 부여에서 면역을 사용하지 않는다. 알 수 없는 비트·비정수/비정상 Spec 메타데이터는 거절한다. 면역은 항상 서버에서 판정하며 Player/NPC가 같은 CombatControlComponent와 GAS 태그를 사용한다.

| 설정 | 차단하는 반응 | 허용되는 반응 예 |
|---|---|---|
| HitReaction | 기존 경직 이벤트/반응 GA 및 공용 일반 피격 표시 | 스턴·넉백·에어본·둔화 |
| Displacement | 수평 넉백 및 에어본 시퀀스의 신규 적용 | 경직·스턴·둔화 |
| StatusEffects | 현재 구현된 스턴·둔화의 신규 적용 | 경직·넉백·에어본 |
| Stun / Knockback / Airborne / Slow | 선택한 개별 종류 | 선택하지 않은 종류 |

예를 들어 HitReaction + Displacement를 선택하면 경직과 날아가기는 막지만 스턴은 걸린다. 면역 여부가 이미 승인된 HP 피해를 취소하지 않는다. 피해 면역/무적 태그, 방어·보호막과 별개다. 면역 부여는 기존 상태의 정화도 아니며, 활성 스턴/다운/둔화를 지우지 않는다. 기존 공중 시퀀스 중간에 면역을 얻어도 착지·다운을 건너뛰지 않는다.

기존 비취소 전투 Ability의 제어 거절 규칙은 유지한다. 종류별 면역을 선택할 때는 취소 가능한 능력에 이 설정을 사용하며, `SetCanBeCanceled(false)` 자체가 종류별 면역을 의미하지 않는다. SkillContainer의 면역 원본은 SkillDefinition → RuntimeSpec이며 일반 GA 부여 데이터의 면역 설정을 여기에 추가로 합산하지 않는다.

새 그룹 태그는 `Shared.Immunity.HitReaction`, `Shared.Immunity.Displacement`, `Shared.Immunity.StatusEffects`다. 기존 `Shared.Immunity.Control.Stun/Knockback/Airborne`와 `Shared.Immunity.Movement.Slow`도 계속 개별 면역으로 동작한다. 새 그룹 태그는 공통 정책에서 해석하며 GAS의 부모/자식 자동 매칭을 역방향으로 가정하지 않는다. 이후 출혈/중독 등의 상태를 추가할 때는 종류와 정책을 명시적으로 확장해야 한다. 아직 구현하지 않은 상태까지 차단한다고 주장하지 않는다.

대상의 `DefaultImmunities`는 기본 면역 설정이다. 기존 대상별 4개 기본 면역 필드는 콘텐츠 호환을 위해 유지하며 OR로 합성한다. 보스는 기존 스턴·넉백·에어본·둔화 면역에 일반 경직 기본 면역을 더한다. 대상 BP에서 이 기본값을 명시적으로 바꿀 수 있다. 기본 설정은 같은 클래스의 구성값이며 임시 서버 상태는 GE를 사용한다.

### 21.2 실행과 수명

- 일반 공유 GA는 활성화 후 BP 실행 전에 부여 데이터/클래스의 면역 설정을 고정한다. SkillContainer는 RuntimeSpec에 설정을 고정하고 자원/쿨다운 commit 성공 후 실행 시작 전에 부여한다. 대상 선택 대기·부여만 된 상태·쿨다운 거절에는 새 면역을 주지 않는다. 원격 예측 클라이언트가 자체 면역 GE나 Loose Tag를 만들지 않는다.
- 네이티브 Infinite GE가 태그의 원본이다. 별도의 만료 타이머를 만들지 않고, 실행마다 GUID와 정확한 GE 핸들을 연결한다. 여러 능력의 동일 면역은 GAS 태그 카운트로 겹치며 하나의 종료가 다른 출처를 제거하지 않는다. NonInstanced/LocalOnly 능력은 이 서버 실행 면역을 부여할 수 없다.
- 정상 종료·취소·부여 회수는 해당 실행 핸들만 해제한다. 사망·권한 회수·Avatar 교체/재사용·EndPlay는 이 수신기가 소유한 실행 면역만 정리한다. 외부의 같은 클래스 GE·아이템 버프·다른 제어 상태·이동 승인은 일괄 제거하지 않는다.
- GE 적용 전 실행 토큰을 예약한다. 적용 콜백이 능력을 종료하거나 생명을 교체하면 뒤늦게 반환된 핸들도 제거한다. 제거 콜백 중 재진입은 종료 중 표시와 정확한 토큰으로 처리하며 새 실행의 핸들을 덮지 않는다. 대상당 동시 실행 면역은 32개로 제한한다.
- 일반 경직은 공용 승인 피해 라우팅에서 기존 이벤트를 소비하고, 반응 GA의 직접/이벤트 활성화에서도 면역을 검사한다. 기존 반응 태그/Trigger를 선언한 GA가 이 검사 대상이다. 공용 표시 수신기와 기존 투사체의 경직 이벤트도 같은 판정을 사용한다. 스턴을 경직으로 바꾸거나 기존 경직 BP의 제어 동작을 제거하지 않는다. 새 기간형 경직 런타임과 피격 애니메이션 전체 재작업은 이번 범위가 아니다.
- 이 단계는 GA 실행 전체 동안의 선택형 면역이다. 몽타주의 특정 구간만 면역을 주는 Notify/구간 API와 면역 전용 HUD/이펙트는 별도 작업이다.

### 21.3 검증 기록

**소스 구현과 분리 빌드 검증을 완료했다. 아래는 최초 통합 보류 기록이며, 이후 현재 프로젝트 통합 완료는 22절을 따른다.**

- 검증용 `Saved/CombatReactionDev/BuildProject`의 Editor 전체 타깃 빌드가 성공했다. 최종 DLL은 `83AAD1381A6BBAFA354E45515C7464AD74A11E2010C2979757B3D98430647F37`이며 Source 947개를 포함한 입력 1610개가 이 분리 빌드 스냅샷과 일치한다.
- **같은 DLL에서 native 72개가 모두 Success/종료 0**이다. 기존 65개와 새 면역 7개로, 분류/설정 고정·실제 장착 SkillContainer의 commit/취소/쿨다운·개별 상태 판정·기존 상태의 비정화·다중 실행의 중첩/종료/부여 회수·경직의 승인 라우팅 및 직접/이벤트 활성화 거절·사망/재사용/ActorInfo 교체·GE 적용 콜백 중 취소/리셋과 외부 GE 보존을 확인했다. 같은 능력 인스턴스를 적용 콜백에서 종료 후 즉시 재발동하는 사례도 추가해, 이전 실행의 실패가 새 실행이나 면역을 종료하지 않고 새 GE 하나만 남는지 검사했다. 일반 GA는 예약한 토큰, SkillContainer는 기존 실행 serial을 재확인하도록 보완했다. 증거: `Saved/CombatImmunity/Test-84cadaa8b3ef470dadd57106da6d73dd/Result.json`.
- **같은 DLL의 원격 2인 Iris 검사, 각 NetDriver 80~160ms/5% 손실**이 통과했다. 활성 면역 중 늦은 참가, 양방향 실제 Player/NPC 피해 승인 핸들러에서 피해 유지와 스턴/둔화/수평 넉백/에어본 거절, 중첩 하나의 취소 뒤 잔여 면역, 정상 종료와 외부 면역 보존, 경직+피격이상 면역 밖의 스턴 적용/능력 취소, NPC 면역 종료 후 둔화 적용 및 치명 타격 정리를 확인했다. 소유자는 0~7, 늦은 관찰자는 1~7단계를 관측했다. 보안 위반 0건, 완료 후 추가 2초 동안 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없었다. 증거: `Saved/CombatReactionDev/BuildProject/Saved/Tests/SharedHit/2c712629155b4d038a8faf000601c683/SharedHit.proof.json`.
- 네트워크 면역 실행은 개발용 공용 네이티브 GA의 실제 GAS 활성화/종료를 사용한다. 기존 장착 공격은 Enhanced Input으로 준비하지만, 피해 타이밍은 서버 피해 이벤트 핸들러로 자극한 검사다. 모든 운영 스킬의 원격 예측·몽타주 구간·자연 공격 타이밍을 검증한 것은 아니다. 첫 검사는 같은 클래스 능력 두 개를 같은 틱에 요청해 기존 발동 간격 보안에 거절됐다. 실제 정책의 최소 간격 뒤에 두 번째 능력을 시작하도록 검사 준비만 수정했으며 보안 정책은 유지했다. 초기 실패 로그도 보존했다.
- 공유 인벤토리·스킬·이동 소스의 동시 수정을 여러 차례 추가 빌드에 반영했다. `RPGGameplayAbility_SkillContainer.h`, `RPGSkillTargetingTypes.h`의 추가 변경까지 보존/반영한 마지막 빌드와 위 검증 후, **공유 입력 1610개가 모두 일치하는 시점**을 확보해 적용 파일 6개(DLL/PDB)의 계획을 만들었다. `BundlePlan.json`에 입력/적용 전후 해시와 BuildId 검사를 기록했다.
- 기존 에디터 PID 360의 현재 PIE가 꺼져 있음을 MCP로 확인하고 일반 닫기를 요청했다. 종료 정리가 30초를 넘겨 보조 스크립트는 모듈을 쓰지 않고 중단했다. 이후 `LogExit: Exiting.`과 PID 소멸로 정상 종료를 확인했으며 강제 종료는 하지 않았다. 그 사이 **별도의 던전 멤버십 네트워크 검사 서버/클라이언트 3개가 현재 모듈을 사용하기 시작했다.** 이 프로세스들을 중단하거나 모듈을 교체하지 않고, 기존 둔화 단계 DLL `2CC7545EE846ED0470331D15AF1C6DDD37B20B97AD18765CF506DC361FBCFB95`로 `bossmap` 에디터를 복원했다. `EditorBeforeClose.json`, `ReopenedEditorProcess.json`, `ReopenedEditor.log`에 기록했다. 모듈/콘텐츠 적용은 0개이며 **현재 열린 에디터에는 새 면역이 아직 반영되지 않았다.**

최초 결과는 `Saved/CombatImmunityIntegration/PreviousSnapshotVerification.json`에 보존했다. 당시 `implementationAndSnapshotChecksPassed=true`, `rootIntegration=false`를 구별한다. 현재의 최종 결과는 22절과 `Saved/CombatImmunity/Verification.proof.json`을 따른다. 테스트 단계 오류는 0이며 기존 기동 `Condition failed` 15줄과 NonInstanced 사용 중단 예정 경고는 별도다. 몽타주 구간별 슈퍼아머, 새 기간형 경직, 지속 피해, 원격 예측 운영 스킬 전체·모든 보스 패턴·여러 PC/Steam/Shipping 및 면역 전용 HUD/연출은 추가 작업이다.

### 21.4 최초 보류 시 작성한 통합 절차

별도 던전 검사가 끝나 현재 모듈을 해제한 뒤, 우선 공유 입력/분리 빌드/DLL 해시를 다시 확인한다. 현재 검증한 스냅샷이 유지되면 위 증거를 사용한다. 입력이 바뀌었다면 최신 Source/Config/플러그인/관련 BP를 분리 프로젝트에 다시 맞추고 Editor 전체 타깃을 빌드한다. `Saved/CombatImmunity/Build.ps1`, `Run-Native.ps1`, `Scripts/Network/Run-SharedHitPresentationTest.ps1 -Immunity`를 사용한다. 후자는 분리 프로젝트 경로와 지연·손실 옵션을 명시한다. **DLL이 바뀌면 새 DLL의 native 72개와 2인 네트워크 증거를 다시 확보해야 한다.** 이전 DLL의 통과를 새 DLL의 결과로 대체하지 않는다.

입력/BuildId/해시가 모두 맞으면 `ApplyVerifiedBundle.py plan`으로 구체적인 적용 파일 목록을 만들고, `Apply-ToIdleEditor.ps1`에 해당 native/network 증거 경로를 넘긴다. 이 보조 스크립트는 현재 PIE가 꺼져 있음을 조회하고 일반 닫기만 사용하며, 저장되지 않은 작업으로 닫기가 지연되면 강제 종료하지 않는다. 적용은 정확한 DLL/PDB만 백업/교체한다. 이어서 현재 프로젝트에서 native/2인 네트워크 검사를 수행하고, 이 작업에서 에디터를 닫았다면 복원한 뒤에 `rootIntegration=true`로 기록한다. 이후 22절에서는 현재 프로젝트의 직접 빌드로 통합을 완료했으므로 이전 분리 DLL 적용 계획은 사용하지 않는다.

## 22. 공용 면역의 현재 프로젝트 통합·재검증 완료 — 2026-10-26

**현재 프로젝트의 실행 모듈에 공용 면역을 통합하고 동일 DLL의 검증을 완료했다.** 21절의 통합 대기는 해소됐다.

재개 시 기존 에디터와 던전 멤버십 검사 프로세스는 종료돼 있었고, 공유 프로젝트의 전체 `Project_RPGEditor Win64 Development` 빌드가 진행 중이었다. 이 빌드를 중단하거나 분리 DLL로 덮어쓰지 않고, 성공한 결과물을 그대로 검사했다. 이전 분리 스냅샷과 면역 핵심 소스는 같았으며 추가 변경은 개발용 `RPGDungeonFlowE2EProbe.cpp`였다. 검사 전후 Source 947개를 포함한 입력 1610개의 해시가 일치한다. 원본 소스/콘텐츠를 원복하거나 다른 검사 프로세스를 종료하지 않았다.

- 전체 Editor 타깃 263개 빌드 작업이 `Succeeded`로 끝났다. 로그는 `Saved/BuildChecks/LobbyFrontendVerification-20261026/Editor-LogoutFix-Retry.log`다. 현재 DLL은 **`6428E48045F7818417FE8A3684FCB9F82DF24B7722F9BEA28D097A46E41BE1F6`**다. `Saved/CombatImmunityIntegration/RootInputs.json`에 검증 입력을 기록했다. 이 단계에서 별도로 복사한 DLL/PDB는 0개다.
- **현재 프로젝트에서 native 72개가 모두 Success/종료 0**이다. 새 면역 7개와 기존 전투/부여/스킬/피해 보안/로비 판단 65개를 포함한다. 실제 장착 SkillContainer의 commit/취소/쿨다운, 종류별 면역과 비정화, 실행 중첩/개별 종료, 기존 경직 라우팅/발동 차단, 사망/재사용/ActorInfo 교체 및 적용 콜백에서 같은 인스턴스의 즉시 재발동을 확인했다. 증거: `Saved/CombatImmunity/Test-9dfd7c84824442b79ff4badf185e89aa/Result.json`.
- **같은 현재 DLL의 원격 2인 Iris 검사, 각 NetDriver 80~160ms/5% 손실**도 통과했다. 면역 활성 중 두 번째 클라이언트가 참가한 뒤, 양방향 승인 피해 유지와 선택한 제어의 거절, 중첩 면역 하나의 취소, 정상 종료/외부 GE 보존, 경직+피격이상 면역 밖의 스턴 적용과 능력 취소, NPC 면역 종료 후 둔화, 사망 정리를 확인했다. 소유자의 0~7단계와 늦은 관찰자의 1~7단계, 실제 NetDriver 설정/Iris 활성 및 완료 후 추가 2초 검사가 통과했다. 보안 위반 0건이며 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없다. 증거: `Saved/Tests/SharedHit/2e4c1f22a8924ec69e88a7ed8084286d/SharedHit.proof.json`.
- 모든 검사 로그에서 실제 로드한 `C:/UE5/Project_RPG/Binaries/Win64/UnrealEditor-Project_RPG.dll` 경로를 확인했고 검사 전후 DLL 해시도 동일하다. 분리 빌드의 통과를 현재 DLL의 결과로 대신하지 않았다. 다른 작업의 프런트엔드 검사와 함께 초기화가 느리게 진행됐지만, 검사는 정해진 시간 안에 완료됐고 판정 조건은 유지했다.
- 재개 당시 일반 에디터가 열려 있지 않았으므로 이 단계에서 사용자 에디터를 닫거나 추가로 열지 않았다. 실제 `bossmap` 로드는 위 네트워크 서버/클라이언트에서 검증했다. 자신이 시작한 검사 프로세스만 정리했다.

최종 결과는 `Saved/CombatImmunityIntegration/Verification.proof.json` 및 `Saved/CombatImmunity/Verification.proof.json`이다. **`passedFinalChecks=true`, `rootIntegration=true`**다. 최초 분리 결과는 `PreviousSnapshotVerification.json`에 남겼다. 기존 기동 `Condition failed`와 사용 중단 예정 경고는 실제 테스트 단계 오류와 구분한다. 몽타주 구간별 면역, 새 기간형 경직, 원격 예측 운영 스킬 전체/모든 보스 패턴, 여러 PC·Steam·Shipping·최종 연출은 검증 완료로 표시하지 않는다.

이제 21절의 `ActivationImmunities`/`AbilityImmunities`/대상 기본 면역 설정을 현재 프로젝트에서 사용할 수 있다. 다음 기능은 지속 피해 한 종류이며, 7.2절의 출처 수명·귀속·회차 예산·정화 정책을 확정한 뒤 구현한다.

## 23. 공용 화상 지속 피해 — 2026-10-26

### 23.1 이번 구현의 정책

첫 DoT는 화상이다. 플레이어 `FRPGPlayerAbilitySet.Burn`, NPC 시작 데이터의 `AttackBurns`, `SkillDefinition.Burn`에 `FRPGBurnSettings`를 설정한다. NPC 설정은 가장 가까운 기존 공격 클래스의 값을 사용하며 공격별 GA BP를 추가하지 않는다. 피해량 0은 비활성화이고, 스킬/적중 처리에서 피해 콜백 전에 설정을 복사한다.

- 최초 부여는 기존 팀·범위·능력·피해 승인 경로를 통과한 살아 있는 대상의 적중에서만 호출한다. 직접 ASC에 기간 GE나 틱 GE를 넣어도 서버 스케줄/틱 피해 권한을 만들 수 없다. BP/RPC 부여 API는 추가하지 않는다.
- 부여 후 사거리 이탈, 원래 공격 종료·취소, 공격자의 스턴/피격이상은 이미 승인된 화상을 취소하지 않는다. 기존 근접 HitResult를 재사용하거나 전역 피해 검증을 느슨하게 하지 않는다. 이 예외는 화상 전용 네이티브 틱 permit에만 적용한다.
- 공격자나 대상의 사망·제거·던전 권한 회수·ASC/Avatar/생명 세대 교체 시 종료한다. 출처 Controller가 사라지거나 교체된 경우도 종료한다. 양쪽 중 보스가 있으면 부여 시 전투 번호를 캡처하고, 그 번호와 InCombat 상태가 유지될 때만 실행한다. 전멸·리셋 중이나 다음 전투에는 이전 화상이 남지 않는다. 공격자가 죽은 뒤 계속 타는 규칙은 이번 버전에 없다. 둔화의 출처 제거 뒤 유지 정책과 구별한다.
- 각 인스턴스는 부여 당시 확정한 **정수 최종 피해 1~10000**, 간격 0.5~5초, 간격보다 길고 최대 30초인 기간을 사용한다. 공격력/방어력/치명타를 회차마다 다시 계산하지 않는다. 최초 적중 피해와 별개인 고정 피해다.
- 최초 피해는 한 간격 뒤에 발생한다. 기간 GE가 살아 있고 억제되지 않은 회차만 실행한다. 만료 순간 회차는 보장하지 않는다. 대상당 최대 8개, 인스턴스당 프레임당 최대 1회이며 지연된 회차는 버린다. 최대 기간·최소 간격에서 인스턴스당 60회 미만이다. 억제·무적 중 회차도 소모하며 해제 후 몰아 때리지 않는다.
- 같은 공격자/다른 공격자 모두 독립 인스턴스로 중첩한다. 자동 갱신·무한 연장은 없다. 9번째 부여는 거절하고 적중 영수증을 소비한다. 각 GE가 자기 기간만 소유하며 별도 만료 타이머를 두지 않는다.
- 틱은 기존 AttributeSet의 TakeDamage를 통해 HP/사망/UI 갱신을 실행한다. 치명타·자원 획득·흡혈·HitReact·추가 상태 부여를 재호출하지 않는다. 틱 Context에는 원래 Source ASC가 남는다. 보스 어그로는 실제 HP 감소분과 부여 시 캡처한 EncounterId로 기존 AddThreat를 호출하고 기존 자격·거리·전투 상태 규칙을 따른다. 새 처치 보상/개인 기여도 장부는 만들지 않고 기존 사망·던전 보상 경로를 사용한다.
- `Shared.Immunity.StatusEffects`와 개별 `Shared.Immunity.Status.Burn`이 새 부여를 막는다. 면역 비트 128을 추가하되 기존 비트는 유지한다. 나중에 면역이 생겨도 기존 화상은 정화하지 않는다. `Shared.Status.Invincible`은 해당 회차 피해를 막는다.
- `RemoveOwnedBurn`은 정확한 자기 핸들 하나, 서버 `CleanseBurns`는 이 수신 모듈이 부여한 화상만 제거한다. 같은 클래스의 외부 효과·버프·장비는 보존한다. 정화 중 재부여, 적용 도중 정화, 틱 도중 생명 초기화를 처리한다.
- 최소 복제 NPC도 화상 중첩 수·최종 만료 서버 시각·생명 리비전을 받는다. 소유자/관찰자에 공용 비상호작용 상태 라벨을 표시한다. 클라이언트 시각/라벨은 피해·만료 권한이 아니다. 화상 전용 파티클과 최종 아트는 이번 범위 밖이다.

### 23.2 검증 기록

**현재 프로젝트의 같은 DLL에서 native 79개와 원격 2인 Iris 검증을 완료했다.** 최종 결과는 `Saved/CombatBurn/Verification.proof.json`이며 `rootIntegration=true`다. DLL SHA256은 `3C1CD079FEE5D88B77F99BEB2059B74CD4955AF49600EAC677C8DB6F1D75CCBA`이다. Source 959개를 포함한 입력 1622개의 검사 전후 해시가 같고, 모든 검사 로그에서 실제 현재 프로젝트 DLL 로드 경로를 확인했다.

- `Saved/CombatBurn/Build.log`의 전체 Editor 타깃 빌드/최종 링크가 성공했다. 공유 프로젝트의 다른 검사에 의한 DLL 잠금은 해당 프로세스를 종료하지 않고 해소를 기다렸다. 초기 자기 빌드는 보스 전투 수명 검증 보완 후 중단·재빌드했고, 사용자 에디터나 다른 검사를 강제 종료하지 않았다.
- Native 증거는 `Saved/CombatBurn/Test-1971c0674f0044bda97e9b053abb0612/Result.json`이다. 화상 7개와 기존 72개가 모두 Success/종료 0이다. 설정 동결·메타데이터 검증, 권한 없는 직접 ASC 틱/중첩 실행/타 대상 permit 차용 거절, 공격 종료/사거리/스턴과 이미 승인된 화상의 독립성, 무적, 독립 중첩/8개 제한/정화와 외부 GE 보존, 억제/지연 회차 폐기/자연 만료, 면역·사망·재사용·ActorInfo/Controller 교체·제거, 적용/틱 콜백 재진입, 보스 어그로 귀속과 양쪽 보스의 리셋/전멸 수명을 검사했다. 테스트 단계 오류는 0이다. 기존 기동 Condition failed 15줄은 별도다.
- 네트워크 증거는 `Saved/Tests/SharedHit/d0c9cf10bc6049ca9647115d50a01149/SharedHit.proof.json`이다. **각 NetDriver의 지연 80~160ms·손실 5%**를 확인했다. 소유자의 0~8단계와 화상 중 늦게 참가한 관찰자의 1~8단계가 통과했다. 실제 기존 Player/NPC 피해 핸들러로 부여하고 양방향의 주기적 HP 감소, 두 중첩 중 하나의 제거, 전체 정화 후 피해 중단, 자연 만료, 상태이상 그룹 면역의 새 부여 거절과 최초 피해 유지, 출처 NPC의 HP 0 이후 남은 화상/틱 제거를 확인했다. 양쪽 클라이언트에서 틱 이후 HP 변화, 태그·복제 중첩 수·라벨 위젯 생성/제거를 확인했다. 완료 후 추가 2초도 fatal/assert/ensure/Net·Iris·BP 실행 오류와 보안 위반이 없었다.
- 앞선 분리 보조 링크는 Windows 경로 치환이 적용되지 않아 이전 분리 DLL의 72개만 실행했으므로 **최종 증거에서 제외했다**. 경위와 제외 결과는 `Saved/CombatBurn/PrivateAttemptExcluded.json`에 남겼다. 위 최종 79개/2인 결과는 새 현재 DLL의 실제 실행 결과다.

최초 현재 DLL의 79개/2인 통과 후 다른 작업에서 `RPGCharacterConfigurationWidget.cpp`가 갱신됐다. 이 변경을 보존하고 최신 공유 소스를 다시 빌드해 위 최종 DLL에서 79개와 2인 검사를 재수행했다. 최초 통과는 `Saved/CombatBurn/Verification-FirstPass.proof.json`에 보존했다. 최종 증거는 최신 공유 입력과 일치한다.

이 검사는 승인된 서버 피해 이벤트를 주는 개발 fixture와 HP 10000을 사용하며, 자연스러운 전체 전투나 물리 키보드 입력을 대신하지 않는다. NullRHI라 라벨 위젯의 존재는 확인했지만 최종 화면 픽셀은 검증하지 않았다. 운영 공격/스킬의 화상 수치 배치(기본 0), 전용 이펙트/아이콘/최종 연출, 여러 PC·4인·Steam·Shipping, 중독/출혈·저항·공격자 사망 후 지속 정책은 추가 작업이다. 새 기간형 경직이나 몽타주 구간별 면역을 이번 완료에 포함하지 않는다.

## 24. 몽타주 구간별 공용 면역 — 2026-10-26

### 24.1 저작 방식과 권한

기존 GAS 몽타주 자체의 Notify 트랙에 네이티브 `RPG Combat Immunity Window`를 넣고 범위를 조절한다. `Immunities`는 21절의 공용 비트 설정을 재사용하며 기본값 3은 경직 표시/기존 경직 반응과 변위 면역이다. 스턴·둔화·화상은 이 기본값으로 막지 않는다. Flags=0으로 끄거나 Notify를 삭제한다. 같은 Player/NPC GA·몽타주를 사용하며 몬스터별 면역 GA BP를 추가하지 않는다.

서버의 `URPGAbilitySystemComponent::PlayMontage`가 저작된 구간을 복사하고, 서버가 실제 재생하는 GAS 몽타주의 AnimInstance/재생 인스턴스 번호·능력 실행 GUID·생명/ASC/Avatar를 확인한다. 구간은 몽타주 시간의 `[시작, 끝)`이다. 잘못된 비트·비정상 시간·범위 밖 시간·8개 초과 구간은 구간 면역을 부여하지 않는다. 실행 중 원본 Notify 수정은 현재 재생의 복사본을 바꾸지 않는다.

Notify 콜백은 빠른 재평가 신호이며 Begin/End 짝이나 Notify UObject에 Actor/GE 핸들을 저장하지 않는다. 권한은 서버의 실제 위치와 현재 재생 인스턴스에 있다. 따라서 표시용 Notify의 확률·LOD 필터·서버 콜백 누락으로 면역 여부를 결정하지 않는다. 일반 AnimSequence 내부 Notify, 원시 `AnimInstance.Montage_Play`나 다른 메시에서 재생한 몽타주는 이 계약에 포함하지 않는다. 기존 GAS PlayMontageAndWait/SkillContainer의 재생 경로를 사용한다.

### 24.2 실행·종료 규칙

- 원격 예측 클라이언트는 면역 GE/Loose Tag/구간 타이머를 만들지 않고 서버의 GAS 태그를 읽는다. 부여만 된 능력·LocalOnly·NonInstanced·종료한 능력에는 새 구간 면역을 주지 않는다. 활성화 당시의 ASC/Avatar/생명과 보스 전투 번호도 고정하여, 이전 생명의 활성 능력이 뒤늦게 재생을 요청해도 새 면역을 받지 못하게 한다. SkillContainer는 성공 commit 뒤에만 구간 면역을 허용한다.
- 구간마다 기존 네이티브 면역 GE의 자기 GUID/정확한 핸들을 소유한다. 겹치는 구간, `ActivationImmunities`, 외부 면역 GE와 OR로 합성하며 한 구간이 끝나도 다른 출처는 보존한다. 구간형만 사용하려면 실행 전체 면역을 0으로 두며, 대상의 기본 면역은 그대로 남는다.
- 재생 속도 변경·앞뒤 위치 이동은 현재 위치로 다시 평가한다. 건너뛴 과거 구간을 나중에 재생하지 않는다. 루프/재진입은 현재 구간을 새 핸들로 연다. 별도의 벽시계 만료 타이머를 두지 않는다.
- 일시정지·0 재생 속도·메시 애니메이션 정지 동안은 구간 면역을 해제하고 재개 후 현재 구간을 다시 평가한다. 블렌드아웃 시작·중단·재생 인스턴스 교체·다른 GAS 몽타주·능력 정상 종료/취소·사망·권한 회수·생명/Avatar/ASC/AnimInstance 교체 때 자기 구간만 제거한다. 보스는 캡처한 EncounterId와 InCombat도 확인한다.
- 적용 도중 취소/같은 GA 인스턴스의 즉시 재발동은 실행 GUID와 재생 요청 ID로 구별한다. 아직 Apply가 반환하지 않은 면역도 정확한 Context로 제거하여 새 실행의 효과를 지우지 않는다. 정화/외부 제거로 사라진 구간 GE는 해당 진입에서 매 프레임 재발급하지 않으며 구간을 나갔다 다시 들어가야 새로 시도한다.
- 추적 중 공용 컴포넌트는 메시 갱신 뒤 매 프레임 위치를 확인하고, 서버 피격의 면역 조회 때도 즉시 확인한다. 추적 종료 후 기존 0.05초 갱신 간격으로 돌아간다. 비표시 서버 메시의 `OnlyTickPoseWhenRendered`는 `OnlyTickMontagesWhenNotRendered`로 올려 몽타주가 진행되게 한다. 더 강한 기존 옵션과 Root Motion은 바꾸지 않으며 종료 때 과거 메시 설정을 덮어 복구하지 않는다.

### 24.3 검증 기록

**현재 프로젝트의 같은 실행 DLL에서 native 85개와 원격 2인 Iris 검증을 완료했다.** `Saved/CombatMontageImmunity/Verification.proof.json`의 `rootIntegration=true`는 이 전투 기능의 실제 실행 모듈 통합을 뜻한다. 검증 DLL SHA256은 `82021ADCA4DD7B9A0923E4CA935EA9C082245401AF24083884F0B875F87301B1`이다.

- 최신 공유 프로젝트의 전체 Editor 타깃 빌드가 성공했고, 검사 전후 DLL 해시 및 모든 로그의 실제 현재 프로젝트 DLL 로드 경로를 확인했다. 소스 입력 969개를 포함한 전체 기준 입력은 1632개다. 전투 구현·선택한 회귀·Config/콘텐츠/플러그인 검증 입력의 변동은 0이다. 다른 작업이 검사 뒤 로비 UI/로비 검증 및 선택하지 않은 인벤토리 E2E 검증 파일을 계속 수정했으므로 **전체 공유 소스 동일성은 주장하지 않는다**. 전체 차이는 proof의 `inputDrift`/`concurrentSourceDriftOutsideCombat`, 전투 차이는 `combatInputDrift=[]`, 전체 동기화 여부는 `rootAllSourceSynchronized=false`로 남겼다. 이 변경을 원복하거나 다른 검사·에디터를 종료하지 않았다.
- Native 증거는 `Saved/CombatMontageImmunity/Test-49835814c2c040c5923db09a13531c30/Result.json`이다. **구간 면역 6개와 기존 79개가 모두 Success/종료 0**이다. 저작 데이터/최대 8개 검증과 실행 중 동결, 실제 서버 메시 시간 진행/속도 변경/일시정지/속도 0/메시 정지/자연 종료, 겹침·앞뒤 seek·외부/전체 실행 면역 보존, 정상 취소·이전 생명의 재생 거절·재사용·AnimInstance 교체·사망, GE 적용 도중 취소 후 같은 GA 인스턴스의 즉시 재발동을 확인했다. 실제 획득·장착 SkillContainer의 미커밋 재생/성공 commit/취소/쿨다운 거절도 포함한다. 테스트 단계 오류는 0이고 기존 기동 Condition failed 15줄은 별도다.
- 초기 85개 실행에서는 속도/자연 종료 검사 하나가 실패했다. 검증용으로 복사한 원본 차지 몽타주의 반복 구간 때문에 커서가 예상 범위 밖으로 진행하지 않았으므로, 검증 복사본의 섹션을 순서대로 잇고 자연 블렌드아웃을 켠 뒤 재검사했다. 운영 차지 몽타주 파일은 바꾸지 않았다. 실패 로그는 `Saved/CombatMontageImmunity/Test-c9527a08838e4f1387e2714078e2c5a7/Editor.log`에 보존했다.
- 네트워크 증거는 `Saved/Tests/SharedHit/39e5b11b48cc4a6495106d267024f2c3/SharedHit.proof.json`이다. 실제 NetDriver의 **지연 80~160ms·손실 5%**, 소유자 0~9단계/활성 구간 중 늦은 관찰자 1~9단계, 두 클라이언트의 `ClientScopes=0`을 확인했다. 플레이어의 겹친 67 마스크에서 64 마스크로 이동, 정지/재개, 블렌드아웃과 외부 경직 면역 GE 보존, NPC의 3 마스크에서 면역 밖의 스턴 적용 및 능력 취소, 재발동 뒤 HP 0 정리를 검사했다. 승인 피해는 유지되고 선택한 넉백/둔화는 거절됐다. 보안 위반 0이며 완료 후 추가 2초까지 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없다.

네트워크 검사는 기존 Player/Guardian과 원래 몽타주 에셋을 사용하는 **서버 전용 네이티브 능력 fixture**다. 해당 프로세스 메모리에서만 Notify/섹션을 구성하고 0.001 재생 속도로 구간을 유지하며 Root Motion을 무시했다. 원래 Notify/섹션/블렌드아웃·Root Motion 설정을 정리 때 복구하고 콘텐츠 파일은 저장하지 않는다. 최초 실제 장착 입력과 이후 승인 피해 이벤트는 기존 경로를 사용하되 원격 예측 운영 스킬 전체의 애니메이션 일치·물리 키보드·전체 자연 전투를 검증한 것은 아니다. NullRHI라 화면 픽셀/전용 면역 HUD·VFX, 모든 보스 패턴·여러 PC·4인·Steam/Shipping은 별도다.

전투 기능의 첫 85개/2인 통과 뒤 동시 인벤토리 생산 코드 3개가 갱신돼, 이를 포함해 새 모듈을 빌드하고 위 두 검사를 다시 수행했다. 첫 통과와 당시 전체 입력 변화는 `Saved/CombatMontageImmunity/FirstPassRuntime.json`에 남겼다. 이후 계속 갱신된 비전투 파일은 위와 같이 범위를 명시했다. 운영 몽타주의 면역 구간은 이번에 임의로 배치하지 않았으며 24.1절 절차로 저작한다. 기간형 일반 경직과 중독/출혈은 여전히 후속 기능이다.

## 25. 기간형 일반 경직 — 2026-10-27

### 25.1 데이터와 적용 순서

`FRPGStaggerSettings.Duration`을 플레이어 `FRPGPlayerAbilitySet.Stagger`, NPC 시작 데이터 `AttackStaggers`, `SkillDefinition.Stagger`에 설정한다. 0은 비활성화이며 활성 범위는 0.05~2초다. NPC는 가장 가까운 부모 공격 클래스 설정을 상속한다. 기존 GA와 SkillContainer를 사용하며 몬스터별 경직 GA BP를 추가하지 않는다. 부여 데이터와 스킬 실행 스냅샷은 기존 방식대로 복사하고, 피해 콜백 전에 경직 설정도 고정한다.

일반 경직은 짧은 행동 제약이다. 스턴 상태이상이나 표시 전용 HitReact와 구분한다. `Shared.Immunity.HitReaction` 및 공용 면역 비트 1이 경직을 막으며 StatusEffects 비트 4는 경직을 막지 않는다. 기본 면역과 능력 실행/몽타주 구간 면역을 그대로 재사용한다. 면역은 승인된 HP 피해를 취소하지 않는다.

기존 서버 적중 승인과 살아 있는 대상 검사를 통과한 뒤 공용 CombatControlComponent에 적용한다. 넉백/에어본 → 스턴 → 일반 경직 순서로 판정한다. 실제 상위 제어가 존재하면 경직을 거절하고 이미 부여한 자기 경직 GE도 정리한다. 상위 제어가 면역으로 거절됐다면 경직은 자기 면역에 따라 독립적으로 판정한다. 명시적으로 경직을 설정한 공격은 기존 경직 이벤트를 소비하여 이중 반응을 막는다. 새 BP/RPC 부여 API는 만들지 않는다.

### 25.2 서버 수명과 공용 행동 제약

- 네이티브 기간 GE `RPGStaggerEffect`가 `Shared.Status.Control.Staggered`를 부여한다. GE 만료가 해제의 기준이며 클라이언트 타이머를 권한으로 사용하지 않는다.
- 중첩은 독립 GE다. 표시 종료 시각은 억제되지 않은 효과 중 가장 늦은 종료를 사용하므로 짧은 새 적중이 긴 경직을 줄이지 않는다. 대상당 자기 효과 최대 16개, 최근 영수증 64개로 제한한다. 중복·면역·상위 제어 거절도 같은 영수증을 다시 사용할 수 없다.
- 공통 `IsActionBlocked`에 연결해 기존 이동·입력·능력 활성화·피해 승인·AI MoveTo/회전 제약을 재사용한다. 진행 중 취소 가능한 전투 능력만 종료하고 기존 취소 불가 능력, 패시브, 사망 능력 정책을 유지한다. 중력과 외부 변위는 계속 처리하며 MovementMode를 강제로 None으로 바꾸지 않는다.
- 정확한 GE 핸들로 자기 효과 하나를 해제할 수 있다. 초기화/사망/재사용/ASC 교체에서는 자기 SourceObject의 효과만 정리한다. 같은 GE 클래스라도 외부 소유 효과는 제거하지 않는다. 적용 중 콜백의 초기화·사망·억제를 다시 검증하여 고아 효과를 남기지 않는다.
- 적중 부여 전후에는 공격자/대상의 생명과 ASC가 같아야 한다. 부여가 완료된 경직은 공격자가 사라져도 자기 기간 동안 유지한다. 대상의 사망·재사용에는 남기지 않는다. HP와 사망의 소유자는 기존 AttributeSet/ASC다.

### 25.3 복제와 표시

서버의 활성 여부·시작/종료 시각·피격 방향·실행 번호·재사용 번호를 읽기 전용 상태로 복제한다. 늦은 참가자도 현재 상태를 재구성한다. 서버 GE/태그가 행동 제약의 기준이며 표시의 재생 여부는 제약을 해제하지 않는다.

표시는 대상의 기존 HitReactionProfile에서 안전한 방향별 몽타주를 선택한다. Gameplay Notify/Root Motion이 있는 몽타주, 호환되지 않는 스켈레톤, 다른 몽타주/능력이 차지한 슬롯은 건너뛴다. 서버 시각으로 남은 구간부터 재생하고 자기 AnimInstance/몽타주 인스턴스만 종료한다. 전용 스턴 VFX는 켜지 않는다. 최종 경직 포즈와 운영 공격별 기간은 콘텐츠 검토 후 배치한다.

### 25.4 현재 프로젝트 검증 완료

현재 프로젝트 `Project_RPGEditor Win64 Development` 빌드가 성공했다. DLL SHA256은 `04D85273D156439A591BBF20D945F2A28ABB86BD85A9DC51B1C31A3AF723B690`이며 다음 검사를 같은 DLL에서 통과했다.

- **네이티브 91개 전부 Success, 종료 0.** 새 경직 검사 6개는 데이터 고정, 이동/중력과 능력 범위, 중첩·억제·정확한 해제, 면역/상위 제어, 적용 콜백·사망·재사용, 라우팅·중복·예산을 다룬다. 실제 스턴·수평 넉백·에어본 적용이 자기 경직을 정리하는 것도 확인했다. 나머지 85개는 기존 전투·장비·스킬·피해 보안·로비 진단 회귀 검사다. 증거: `Saved/CombatStagger/Test-d3c46157c6c64a46b4fe7412e2483ff7/Result.json`.
- **원격 2인 Iris, 각 NetDriver 지연 80~160ms·손실 5%.** 실제 Player/Guardian의 원래 공격 피해 경로와 Enhanced Input을 사용했다. 첫 경직이 취소 가능한 장착 공격을 끝내고 이동을 차단하는 동안 관찰자가 늦게 참가했다. 양쪽 클라이언트가 활성 상태/태그를 확인한 후 자연 만료와 실제 이동 재개, Player→NPC 경직과 AI MoveTo 거절, 겹친 짧은 경직의 정확한 제거, 일반 경직 면역과 상태이상 면역의 구분, 상위 스턴과 사망 정리를 확인했다. 소유자 0~9단계와 늦은 관찰자 1~9단계가 모두 완료됐다. 보안 위반 0이며 완료 후 추가 2초까지 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없다. 증거: `Saved/Tests/SharedHit/f1ef2fc612ad441ea92b2f6585b23d70/SharedHit.proof.json`.
- 실제 로드한 현재 프로젝트 DLL 경로와 해시가 양쪽 검사에서 일치한다. Source 976개를 포함한 입력 1639개가 검증 전후 동일하다. 최종 증거는 `Saved/CombatStagger/Verification.proof.json`이며 `passedFinalChecks=true`, `rootIntegration=true`, `inputDrift=[]`다. 기존 기동 단계의 `Condition failed` 15줄과 사용 중단 예정 경고는 테스트 단계 오류 0과 구분한다.

네트워크는 서버 승인 피해 이벤트를 주는 개발 fixture와 HP 10000/치명타용 HP 1을 사용한다. 주 제어 기간은 2초이며 짧은 중첩 0.5초를 검사했다. 0.05초 하한의 모든 지연 조건, 자연 전투 전체, 원격 예측 운영 스킬 전체를 검증한 것은 아니다. NullRHI에서 몽타주 인스턴스 로그는 확인했지만 NPC를 포함한 최종 포즈·화면 픽셀·전용 연출은 검증하지 않았다. 운영 공격별 경직 수치(기본 0), 모든 보스 패턴 조합, 여러 PC·4인·Steam·Shipping 검증은 별도다.

재실행은 `Saved/CombatStagger/Run-Native.ps1`과 `Scripts/Network/Run-SharedHitPresentationTest.ps1 -ProjectRoot C:/UE5/Project_RPG -Stagger -PacketLagMinMilliseconds 80 -PacketLagMaxMilliseconds 160 -PacketLossPercent 5 -TimeoutSeconds 600`을 사용한다. 다음 기능 후보는 중독/출혈 확장, 운영 공격 데이터 저작, 에어본의 늦은 참가·복잡한 지형 검증이다.

## 26. 대표 공격의 경직 데이터 연결 — 2026-10-27

### 26.1 적용 값과 범위

공용 경직을 실제 사용 중인 부여 데이터에 연결했다. 아래 값은 플레이 감각을 조정하기 위한 초기값이며 최종 밸런스 확정값은 아니다.

| 대상 | 실제 데이터 | 기존 공격 | 경직 시간 |
|---|---|---|---|
| 플레이어 대검 | `BP_PlayerWeapon.PlayerWeaponData.DefaultWeaponAbilities` | `InputTag.LightAttack.GreatSword`의 `GA_Player_DefaultAttack` | 0.25초 |
| Guardian | `DA_Guardian.AttackStaggers` | `GA_Guardian_Melee1` | 0.35초 |

대검의 다른 부여 항목·입력 태그·능력 클래스·피해 곡선·특수 능력 데이터는 변경 전후 의미 비교로 동일함을 확인했다. Guardian은 기존 시작 데이터의 해당 공격 맵 항목만 추가했다. 두 번째 근접 공격이 첫 공격 클래스를 상속하지 않는 것도 재로드 검사에서 확인했다. 기존 반응 정책은 유지하고 25절의 명시적 경직 라우팅이 기존 반응 이벤트를 소비한다. 보스의 경직 기본 면역은 기존 규칙을 사용한다.

### 26.2 저작과 확인 방법

열려 있던 에디터에서 PIE 미실행과 대상 패키지 두 개의 미저장 변경 부재를 먼저 확인했다. `Saved/Backups/SharedStaggerAttacks/`에 원본을 보관한 뒤 대상 속성만 수정하고 무기 BP 컴파일·저장을 수행했다. 실제 변경 전후 속성/해시 기록은 `Saved/CombatStaggerContent/Before.json`, `WeaponAfter.json`, `GuardianAfter.json`에 있다.

재현용 도구는 `Scripts/Editor/ConfigureSharedStaggerAttacks.py`다. 기본 실행은 읽기 전용 재로드 검사이며, `-RPGConfigureSharedStaggerAttacks`를 명시한 경우에만 초기 프리셋을 적용한다. PIE·미저장 데이터·사용자가 지정한 다른 시간은 보존하도록 중단하며, 이미 같은 값이면 저장하지 않는다. 이 프리셋 도구는 두 대표 공격의 초기 설정용이고 런타임의 허용 범위는 계속 0.05~2초다.

새 `-AuthoredStagger` 네트워크 모드는 무기 장착으로 부여된 플레이어 능력과 NPC 시작 데이터가 부여한 기존 능력을 조회한다. 경직 시간을 테스트용 값으로 덮어쓰지 않는다. 원래 서버 피해 핸들러를 통해 적용하고 서버 종료 시각에서 실제 0.25/0.35초를 확인한다. 소유자와 관찰자 모두 활성 태그·행동 제약·자기 경직 몽타주 인스턴스를 확인한 뒤 자연 만료를 확인한다. 기존 몽타주가 자리를 차지한 동안 표시를 기다리며, 짧은 경직의 관측이 완료될 때까지 새 승인 적중을 반복할 수 있다.

### 26.3 테스트 월드 정리 보완

초기 분리 DLL의 native 검사는 두 차례 Water 모듈의 GC 접근 위반으로 중단됐다. 호출 위치를 확인한 결과, 기존 임시 월드들이 `EndPlay` 없이 물리 씬을 제거하고 있었다. 설치된 UE 5.8의 `ABuoyancyManager::EndPlay`는 이 시점에 비동기 출력·물리 콜백을 해제한다.

`RPGTestWorldCleanup.h`에 EndPlay → 월드 제거 → 월드 컨텍스트 제거를 공통화하고 관련 전투/장비/스킬 검사 12개 파일에 적용했다. 플레이가 완전히 시작되지 않은 테스트 월드는 시작된 액터의 EndPlay도 정리한다. 이후 native 91개가 모두 통과했고 테스트 단계의 EndPlay 누락 경고와 오류는 0이다. 중단 기록은 `Test-5fea060d860b41518ffa674b6a8ce59c`, `Test-7e3543fd478a44a4b7d45add8cee7773`에 보존하며 성공 증거에 포함하지 않는다. 같은 빌드의 진행도 복원 검사에는 보호된 override의 상태 조회를 공개 GAS 기본 인터페이스로 호출하도록 컴파일 호환 한 줄도 수정했다. 가상 호출과 판정은 유지한다.

### 26.4 검증 결과와 한계

현재 프로젝트 경로에서 로드한 DLL **`4C076D08EA921387927BE654F4A590BA4362E714F429C9B93FF4C59836CFDC19`**로 다음 세 검사를 통과했다. 전체 Editor 빌드 성공 로그의 사본은 `Saved/CombatStaggerContent/RootBuildSucceeded.log`다.

- Native 91개 전부 Success/종료 0. 증거: `Saved/CombatStaggerContent/Test-a5d06dcd42f643f69f510c6d04840562/Result.json`. 기동 단계의 기존 `Condition failed` 15줄과 테스트 단계 오류 0을 구분한다.
- 실제 부여 데이터 2인 Iris 검사: 추가 지연/손실 0, 0~4단계, 양방향 실제 기간·플레이어 공격 취소·양쪽 경직 몽타주 인스턴스·자연 만료. 증거: `Saved/Tests/SharedHit/9c55e94f14e249e6958044987c7478a0/SharedHit.proof.json`.
- 기존 긴 경직 fixture의 2인 Iris 회귀: 각 NetDriver 지연 80~160ms·손실 5%, 활성 경직 중 늦은 참가, 이동 차단/회복·AI MoveTo·중첩 해제·면역·스턴 우선순위·사망 정리. 증거: `Saved/Tests/SharedHit/02b5623547354459b92d2f2505f17d8a/SharedHit.proof.json`.

실제 데이터 검사는 **경직/피격 표시를 관측하기 위해 Player→NPC 3회, NPC→Player 1회의 승인 피해 이벤트**를 사용했다. 원래 능력과 실제 부여 데이터는 사용하지만, 자연 전투 전체나 모든 0.25초 타격의 표시 보장을 입증한 검사는 아니다. 추가 지연/손실 0의 실제 수치 검사와 긴 기간을 사용하는 지연/손실 검사를 구분한다. 양쪽 검사 모두 완료 뒤 추가 2초까지 fatal/assert/ensure/Net·Iris·BP 실행 오류와 이동 보안 위반이 없었다. 원격 입력은 Enhanced Input 주입이며 물리 키보드·최종 화면 픽셀·전용 연출·여러 PC·4인·Steam·Shipping은 별도다.

이후 동시 작업이 세션 코드·별도 진행도/클라이언트 흐름 검사·ChargeSlash BP·AssultBlade 정의를 갱신하고 실행 DLL도 다시 빌드했다. 별도 DLL `DF9DEE21A76DE15D54C4A89FA37142DD544281A7455C8022AC89CE7BDE043AC8`에서도 실제 데이터 2인 검사를 추가 통과했다(`Saved/Tests/SharedHit/50c97927c17c43f0886cab0ed808b0b9/SharedHit.proof.json`). 최종 기록 시점에는 또 갱신된 DLL이 있으므로 **현재 최신 DLL 전체의 91개/지연 검사 통과나 전체 공유 소스 동일성을 주장하지 않는다.**

최종 증거는 `Saved/CombatStaggerContent/Verification.proof.json`이다. 입력 기준 1648개와 전체 변경 목록, 세 핵심 검사의 동일 DLL, 추가 검사의 별도 DLL, 관측 당시 최신 DLL을 각각 기록했다. 두 대표 공격과 공용 피격/상태 코드의 변동은 없고, `rootAllSourceSynchronized=false`와 `latestRootWholeRegressionVerified=false`를 명시한다. 다른 작업의 소스·자산·프로세스를 보존했으며 이 작업에서 사용자 에디터나 다른 검증을 종료하거나 루트 DLL을 직접 덮어쓰지 않았다. 독립 읽기 전용 자산 재로드도 통과했다(`Saved/CombatStaggerContent/Reload-cb23de150c864ca8a8f06090b0c8dc61/Result.json`).

후속은 실제 플레이에서 두 경직 시간 조정, 공격별 면역 구간 저작, 전용 피격 연출 및 에어본의 복잡한 지형/늦은 참가 검증이다.

## 27. 에어본 경사면·낭떠러지 착지 검증 — 2026-10-27

### 27.1 구현 범위와 판정

기존 공용 에어본의 실제 지형 동작을 검증하는 native 테스트 두 개와 `-AirborneTerrain` 네트워크 모드를 추가했다. 기존 CombatControlComponent·CMC의 착지/다운/기상 규칙을 사용한다. 이번 단계에서 운영 전투 규칙이나 공격 데이터는 수정하지 않았다.

- `ProjectRPG.Combat.Airborne.WalkableSlopeLanding`: 20도 경사면의 실제 충돌 컴포넌트·보행 가능 법선·높아진 착지를 확인한다. 착지 후 강제 이동 소스가 제거되고, 다운/기상 중 이동 입력이 차단되며 정상 회복하는지 검사한다.
- `ProjectRPG.Combat.Airborne.LedgeWaitsForLowerFloor`: 높은 발판에서 300cm 아래 발판으로 날아간다. 이동 곡선 종료 뒤에도 실제 착지 전에는 Airborne·낙하·이동 보안 승인을 유지하고, 아래 발판에 착지한 시각부터 Downed를 시작하는지 확인한다. 로컬 PlayerController로 서버 물리를 실행하고 기본 생성 유예가 지난 뒤 실제 보안 컴포넌트의 위반 0과 승인 해제를 검사한다.

곡선의 종료 시각으로 착지를 대신 판정하지 않는다. 경사면에서는 곡선보다 먼저 착지할 수 있고, 낭떠러지에서는 곡선 이후에도 중력에 의한 낙하가 이어진다. 두 경우 모두 실제 CMC 착지를 기준으로 다운/기상한다.

### 27.2 멀티플레이 검증 방법

`RPGAirborneTerrainProbe.h`가 서버에 복제되는 임시 바닥과 20도 경사면을 만든다. 기존 맵과 에셋은 저장하지 않는다. 기존 맵 충돌과 분리하려고 시작 지점을 3000cm 높이며, 이 준비 이동에만 0.5초/3100cm의 제한된 보안 승인을 사용하고 즉시 종료한다. 이후 피격 이동은 기존 공용 에어본의 승인 수명을 따른다.

NPC→Player는 실제 피격 방향인 공격자의 반대쪽 경사면에 착지한다. 이어서 Player가 경사를 향해 돌아서 공격하고, NPC는 경사 끝을 넘어 낮은 평면 바닥에 착지한다. NPC 배치는 바닥 추적으로 캡슐 높이를 맞추며 기존 팀·거리·승인 피해 이벤트 경로를 사용한다. 테스트용 HP 10000과 직접 능력 부여 fixture를 유지하므로 자연 전투 전체나 실제 장비 입력/적중 창 검증으로 확대하지 않는다.

검사는 Cube 메시의 비동기 컴파일 완료와 양쪽 클라이언트의 바닥 충돌·시작 위치 수신을 기다린다. 준비 위치 보정 RPC는 확인될 때까지 재전송한다. 클라이언트의 동적 액터 이름은 서로 같다고 가정하지 않는다. 서버의 실제 착지 바닥/법선/시각, 양쪽 클라이언트의 0~9단계 에어본·다운·기상 표시/몽타주 인스턴스·입력 제약·위치 수렴, 면역과 이동 보안 위반 0을 확인한다.

재실행 명령은 다음과 같다. `-ProjectRoot`에는 해당 검사를 포함해 빌드한 프로젝트를 명시한다.

```powershell
Scripts/Network/Run-SharedHitPresentationTest.ps1 -ProjectRoot C:/UE5/Project_RPG -AirborneTerrain -Port 19083 -PacketLagMinMilliseconds 80 -PacketLagMaxMilliseconds 160 -PacketLossPercent 5 -TimeoutSeconds 300
```

### 27.3 최종 결과와 증거

공유 프로젝트의 컴파일은 `-NoLink`로 성공했고, 같은 오브젝트를 별도 DLL에 링크해 검증했다. 다른 작업의 에디터가 사용하는 루트 DLL을 교체하지 않았다. 검증 프로젝트는 `Saved/CombatAirborneTerrain/Snapshot-72f22e04a5314423b0baa70e3a30d500`이며 DLL SHA256은 **`FDA35EAEC294B885DE788DDEF8611363ACFFCF38B92DA2E250A1B4C2BFC0F3BB`**다. 아래 두 검사에서 실제 로드 경로와 같은 DLL 해시를 확인했다.

| 검사 | 결과 | 증거 |
|---|---|---|
| Native 회귀 | 신규 2개 포함 93개 모두 Success/종료 0, 테스트 단계 오류 0 | `Saved/CombatAirborneTerrain/Test-4369d9e542d34c7e9f3288b31baeec53/Result.json` |
| 2인 Iris | 각 NetDriver 지연 80~160ms·손실 5%, 두 클라이언트 모두 0~9단계 통과 | 검증 프로젝트의 `Saved/Tests/SharedHit/9de14dc2933f4c97bafbbeb0c22dc861/SharedHit.proof.json` |
| 경사면 실제 착지 | 법선 Z=0.939693, 시작 후 1.72703초에 착지, 곡선 1.80702초보다 먼저 종료 | 같은 실행의 `server.log` |
| 낭떠러지 아래 착지 | 시작 후 1.98985초에 착지, 곡선 1.80702초 이후 낙하 유지 | 같은 실행의 `server.log` |

최종 감사는 `Saved/CombatAirborneTerrain/Verification.proof.json`, 재감사 도구는 같은 폴더의 `Audit.py`다. 복사한 Source·Config·Content/Blueprints 입력 1379개의 해시가 유지됨을 확인했다. 그 밖의 Content와 Plugins는 원본에 연결되어 있으므로 전체 프로젝트 입력 동결을 주장하지 않는다. 루트의 동시 변경은 별도 목록으로 남겼고 **`rootIntegration=false`, `isolatedRuntimeVerification=true`, `latestRootWholeRegressionVerified=false`**로 구분한다.

이전 실패는 성공 증거에서 제외했다. 로컬 제어가 아닌 native 더미 Controller, 비동기 메시 준비 전 바닥 조회, 클라이언트 액터 이름에 의존한 준비 검사, 실제 피격 반대쪽에 배치한 경사면을 각각 테스트 설정에서 수정했다. 실패 스냅샷/로그는 같은 작업 폴더에 보존했다. 다른 작업의 프로세스·소스·자산은 유지했다.

기동 단계의 기존 native `Condition failed` 15줄과 Iris 모듈 추가 로드 경고(서버 3/클라이언트 각 2)는 별도 기록했다. 완료 후 추가 2초까지 fatal/assert/ensure/Net·Iris·BP 실행 오류와 이동 보안 위반은 없었다. 모든 기동 경고가 0이라는 의미는 아니다.

### 27.4 남은 범위

이번 네트워크 실행은 NullRHI이며 두 클라이언트가 에어본 시작 전에 참가했다. **에어본 중 늦은 참가**, 급경사/보행 불가 면·이동 발판·복잡한 전투 조합, 여러 PC/4인·Steam·Shipping, 최종 화면 픽셀과 Guardian 전용 다운/기상 연출은 후속이다. 다음 기능은 에어본 진행 중 늦은 참가자의 상태·시각·위치·회복 동기화 검증으로 이어간다.

## 28. 에어본·다운·기상 중 늦은 참가 검증 — 2026-10-29

### 28.1 검사 구성과 권한

`Run-SharedHitPresentationTest.ps1 -AirborneLateJoin`에 `-AirborneLateJoinTarget Player|NPC`, `-AirborneLateJoinPhase Airborne|Downed|GettingUp`를 추가했다. 대상 두 종류와 초기 단계 세 종류를 조합해 여섯 번 별도로 실행한다. 기존 ASC/CombatControlComponent의 복제 스냅샷과 서버 시각·CMC·자기 몽타주 인스턴스를 사용하며 새 피격 GA BP나 클라이언트 상태 부여 RPC는 만들지 않는다.

두 번째 클라이언트는 실제 서버에 연결해 보스 맵 로딩을 마친 뒤, UE의 `UGameInstance::DelayCompletionOfPendingNetGameTravel` 훅에서 게임플레이 입장 완료를 기다린다. 이 훅은 엔진이 `NMT_Join`을 보내 플레이어를 생성하기 전에 호출한다. 기존 BP GameInstance를 유지하고, 네이티브 부모의 override는 두 개발 플래그가 명시된 두 번째 클라이언트에서만 검사 신호를 처리한다. 일반 실행은 부모 기본 동작을 따르며 Shipping은 검사 함수를 호출하지 않는다.

검사 GameInstance가 Player/Guardian/Boss 클래스와 하드 참조 메시·반응 프로필, 런타임 GameplayCue 클래스의 로딩을 준비한 뒤 해당 프로젝트 `Saved`의 실행별 디렉터리에 `map.ready`를 쓴다. 큐 레지스트리 스캔과 클래스 로딩은 개발 검사 훅에서만 선행하며 일반 GameplayCueManager 설정을 변경하지 않는다. 서버는 이 준비가 끝난 뒤 원래 승인 피해로 시퀀스를 시작한다. 지정 단계에 진입한 후 0.15초에 `join.ready`를 쓰고, 두 번째 클라이언트는 훅을 통과해 실제 `NMT_Join`을 보낸다. **전송 연결은 피격 전에 존재하고, 플레이어 생성/초기 스냅샷 수신은 피격 도중 발생하는 검사**다. 캐릭터 제거 검사의 이동 패킷 배출용 1초 유예도 전투 검사에는 적용하지 않도록 정리했다.

서버의 월드·전투를 일시 정지하거나 시간 배율을 바꾸지 않는다. 기다리는 동안 피격 재부여·타이머 갱신·단계 강제 전환도 하지 않는다. 실제 단계가 지나갈 때까지 늦은 참가자가 확인하지 못하면 실패로 남긴다. NPC를 검사할 때는 먼저 접속한 한 명으로 플레이어 피격/회복을 진행하고, 이어지는 NPC 피격의 지정 단계에서 두 번째 클라이언트를 접속시킨다.

### 28.2 관측용 데이터와 확인 조건

플레이어 대상 늦은 참가 뒤 이어지는 NPC 공중 피격 회귀에도 관측용 중력 0.32를 사용한다. 다운/기상 중 초기 접속을 검증하는 원래 대상의 중력은 1이며, 후속 NPC 비행의 관측 조건과 구별한다.

기존 데이터 검증 범위 안에서 높이 600cm·수평 거리 300cm·다운 4초·기상 3초를 사용한다. 공중 초기 복제 두 경우에는 캐릭터/관찰 대상의 초기 복제가 끝날 때까지 관측할 수 있도록 검사 전용 `GravityScale=0.32`를 사용한다. 기존 비행 계산식의 약 3.912초이며 기존 4초 상한 안이다. 다운/기상 초기 복제 네 경우는 기본 중력이다. 운영 공격 수치·기본 중력·에셋은 저장하거나 변경하지 않는다. 앞선 독립 실행 맵 준비/접속 방식과 기본 중력의 짧은 공중 관측 시도는 실제 로딩·초기 복제 시간으로 단계를 놓쳤으므로 최종 성공 증거에서 제외한다.

서버는 실제 `NMT_Join`으로 생성된 관찰자 컨트롤러를 검사 대상에 등록한다. 관찰자는 자기 캐릭터의 입력 초기화가 끝나기 전에도 대상의 상태를 검사할 수 있고, 자기 캐릭터의 입장/입력 준비 완료는 별도로 기다린다. 준비 대기 중 공격이 끝나지 않도록 첫 공격 입력도 `map.ready` 이후에 시작한다.

늦게 생성된 관찰자 Pawn은 최초 넉백 fixture의 경로 정리 때 존재하지 않았다. 플레이어 회복 뒤 후속 NPC 공격을 시작하기 전에 관찰자를 경로 옆 700cm 지점으로 옮기고, 관찰자 본인의 제한된 이동 승인과 소유자 위치 보정 RPC를 사용한다. 원래 피격 대상의 충돌·보안 판정이나 활성 단계의 기간은 바꾸지 않는다. 서버가 실제 충돌 후 이동 소스를 종료한 경우까지 무조건 RMS가 있어야 한다고 검사하던 중간 실행은 성공으로 집계하지 않는다.

- 서버가 접속 신호를 쓸 때 기록한 실행 번호·최초 피격 시작 시각·현재 단계 시작 시각과 늦은 참가자의 스냅샷을 대조한다. 다른 피격을 새로 받은 결과와 구분한다.
- 현재 단계 태그·행동 제약·자기 몽타주 인스턴스, 서버 시각에서 계산한 애니메이션 진행 위치를 확인한다. 최초 시각 동기화와 왕복 지연을 고려해 몽타주/Root Motion Source 진행 시간은 서버 경과 시간에 대해 0.5초까지 허용한다. 실행 번호와 원래/단계 시작 시각 자체는 서버 기록과 일치해야 한다.
- 피격 단계와 CMC 위치 보정이 서로 다른 패킷으로 도착할 수 있으므로 착지 직후 위치 차이는 수렴을 기다린다. 소유자의 이동 입력을 주입한 상태에서 지상 가속도가 0인지 확인하고, 정해진 단계가 끝나기 전에 위치 수렴·입력 차단 확인을 받지 못하면 실패한다.
- 먼저 접속한 클라이언트는 0~9단계, 늦은 참가자는 해당 초기 단계부터 9단계까지 확인한다. 실제 착지·다운·기상·회복, 소유자 입력 차단/공격 취소, 위치 수렴·면역과 이동 보안 위반 0을 기존 검사와 함께 확인한다.
- 각 실행의 서버/두 클라이언트가 Iris와 지연 80~160ms·손실 5%를 실제 NetDriver에서 사용하는지 확인하고, 완료 뒤 추가 2초의 fatal/assert/ensure/Net·Iris·BP 실행 오류도 검사한다.

재실행 예시는 다음과 같다. `Player`를 `NPC`로, `Airborne`을 `Downed`/`GettingUp`으로 바꾸어 나머지 조합을 실행한다. 지형 검사 모드와는 별도 실행한다.

```powershell
Scripts/Network/Run-SharedHitPresentationTest.ps1 -ProjectRoot C:/UE5/AJ_1a95d0 -AirborneLateJoin -AirborneLateJoinTarget Player -AirborneLateJoinPhase Airborne -Port 19091 -PacketLagMinMilliseconds 80 -PacketLagMaxMilliseconds 160 -PacketLossPercent 5 -TimeoutSeconds 300
```

### 28.3 초기 시각과 연출 재동기화 보완

실제 지연/손실 실행에서 늦은 참가자의 다운 상태·태그·위치는 맞지만 몽타주 진행 위치가 서버 시각보다 뒤처지는 경우를 확인했다. 기존 구현은 최초 재생 때만 진행 위치를 정하고 같은 단계에서는 즉시 반환했다. 초기 서버 시각 추정이 나중에 보정돼도 재생 위치는 갱신되지 않았다.

공용 `RPGCombatAirborne.cpp`에서 다음을 보완했다. 플레이어와 NPC가 같은 경로를 사용한다.

- 복제된 시작 시각보다 현재 시각 추정이 이르면 강제 이동/몽타주를 경과 시간 0으로 시작하지 않고 시각 수신을 기다린다.
- 같은 단계에서도 자기 몽타주 인스턴스의 위치를 서버 경과 시간과 비교한다. 0.1초보다 벌어지면 해당 인스턴스만 현재 위치로 보정한다. 다른 시스템이 같은 에셋을 새로 재생한 인스턴스는 보존한다.
- CMC가 이미 같은 생명/실행 번호의 Root Motion Source를 수신했다면 기존 소스를 자기 추적 이름으로 인계한다. 새 소스를 중복 생성하지 않고 이후 정리에 포함한다.
- 초기 메시/애니메이션 설정으로 AnimInstance가 바뀌면 이전 인스턴스의 자기 표시만 정리하고, 같은 단계라도 새 인스턴스에서 현재 경과 위치를 복원한다. 새 인스턴스의 외부 몽타주가 재생 중이면 준비될 때까지 기다린다. 동일 AnimInstance의 외부 재생은 재시작하지 않는다.

NPC 초기 참가에서는 별도의 충돌도 확인했다. `BP_RPGNPCBase`의 BeginPlay가 `PlayAnimMontage`로 등장 연출을 시작해, 이미 복제된 공중 피격 포즈를 덮어쓰고 등장 Root Motion이 공용 이동 소스를 막았다. Blueprint 그래프를 저장 없이 읽어 경로를 확인했다(`Saved/CombatAirborneLateJoin/Audit-15ac3d8bcdd0404fbb0843011ac3145c/Editor.log`). 공통 NPC의 기존 가상 `PlayAnimMontage`를 재정의해 **비권한 복제자의 BeginPlay 중 이미 제어 스냅샷/태그가 활성인 경우**에만 초기 호출을 건너뛴다. 정상 등장과 BeginPlay 이후의 별도 몽타주 호출은 유지하며, NPC별 GA/BP를 복제하거나 운영 에셋을 수정하지 않는다. 실제 Guardian BP의 지연 생성으로 세 단계와 정상 등장을 비교하고 이후 외부 호출 보존도 같은 native 검사에 포함했다.

안전한 공용 피격 몽타주만 대상으로 하며 서버 피해·면역 판정·단계 GE의 수명과 실제 착지 조건은 유지한다. 새 native `FutureClockAndOwnedPoseResync`는 미래 시각 대기, 같은 실행/시작 시각에서 경과 시간 복원, 기존 이동 소스 재사용, 재생 인스턴스 유지, AnimInstance 교체 후 재동기화와 외부 재생/정리 보존을 검사한다. 초기 스냅샷 fixture는 이전 몽타주의 블렌드아웃 목록을 물리 틱 없이 제거하기 위해 애니메이션 수명을 재초기화한다.

기존 스킬 검사 네 개는 복사한 ChargeSlash의 운영 쿨다운 12초를 읽으면서 1초 후 재입력을 기대해 실패했다. 이 검사들의 transient 정의에만 1초를 명시해 원래 검사 조건을 고정했다. 운영 에셋과 실제 스킬 수치는 변경하지 않았다.

### 28.4 검증 결과

분리 프로젝트는 `C:/UE5/AJ_1a95d0`이며 물리 경로는 `Saved/CombatAirborneLateJoin/Snapshot-34bd21659ad14b55b5ecea04d553f8ce`다. 전체 Editor 모듈 빌드 605개 작업 성공 뒤 변경한 C++ 여섯 개를 같은 컴파일러/응답 파일로 다시 컴파일하고 링크했다. NPC 가상 함수 변경에 필요한 생성 코드와 native 보스 코드도 갱신했다. 전체를 다시 컴파일하던 `DependentCompile.log`는 중단한 실행이며 전체 성공 증거로 사용하지 않는다. 공유 플러그인 import library 여섯 개는 별도 복사본의 해시를 고정해 링크했다. 빌드 로그는 `BaseIsolatedBuildSucceeded.log`, `IncrementalCompileLink.log`, 링크 입력은 `PinnedLinkInputs.json`에 보존한다. 일부 기존 오브젝트의 PCH 디버그 정보가 바뀌어 LNK4206 경고가 있으며, 해당 오브젝트의 디버그 심볼 품질은 제한된다. 링크 성공과 런타임 검증을 모든 빌드 경고 0으로 표현하지 않는다.

검증 DLL SHA256은 **`594B8567567205BD3A9FC22D2296982D579108D1DE0705D3E1B9FE32A34AB161`**다. 같은 DLL에서 신규 검사 포함 native 94개가 모두 Success/종료 0으로 끝났다. 테스트 시작 이후 오류는 0이며, 기존 기동 단계 `Condition failed` 15줄은 별도 집계한다. 증거는 `Saved/CombatAirborneLateJoin/Test-271e780c43534bac9498bd6bf16f7596/Result.json`이다.

같은 DLL에서 Player/NPC × Airborne/Downed/GettingUp 여섯 조합이 모두 통과했다. 서버와 두 클라이언트의 Iris 및 실제 NetDriver 지연 80~160ms/손실 5%를 확인했다. 원래 실행/단계 시작 시각, 초기 단계의 현재 포즈/이동 진행, 실제 착지·다운·기상·회복과 체크포인트를 대조했고 이동 보안 위반은 0이다.

2026-10-29 재개 시에는 조율 표에 분리 프로젝트/DLL·전용 포트를 등록했다. 10월 28일 통과한 다섯 조합과 native 94개 결과를 보존하고, 중단됐던 NPC 공중 참가 한 조합만 같은 DLL에서 실행했다. 재빌드·공유 DLL 교체·C++/에셋 추가 수정은 하지 않았다. 조율 표의 본인 행 외 공용 로드맵 진행 항목은 수정하지 않았다.

| 대상 | 최초 관측 단계 | 단계 경과(초) | 포즈 오차(초) | 증거 run ID |
|---|---|---:|---:|---|
| Player | Airborne | 1.658 | 0.116 | `bad7ac602fbb4b9792484977d8b7774f` |
| Player | Downed | 1.640 | 0.000 | `32f4769c14cf4db5afa0416098d3ddd5` |
| Player | GettingUp | 1.160 | 0.000 | `ea8f2172b6514131b1dfd633e2328957` |
| NPC | Airborne | 1.717 | 0.075 | `e98d353a07c34e8f8a779f387720e43e` |
| NPC | Downed | 1.049 | 0.000 | `e195d9852af04f02a2789dfcf4af1023` |
| NPC | GettingUp | 0.976 | 0.327 | `8795355422414f8f96dd568a5f7e1c79` |

각 run의 `SharedHit.proof.json`과 서버/양쪽 클라이언트 로그는 검증 프로젝트의 `Saved/Tests/SharedHit/<run ID>`에 있다. 최종 감사는 `Saved/CombatAirborneLateJoin/Verification.proof.json`, 재감사 도구는 같은 폴더의 `Audit.py`다.

복사한 Source·Config·Content/Blueprints 입력 1383개의 해시 변동은 0이며, 공유 플러그인 import library 여섯 개의 고정 해시도 확인했다. 테스트에 실제 로드한 DLL 경로와 해시는 native/여섯 네트워크 실행 모두 일치한다. `rootIntegration=false`, `isolatedRuntimeVerification=true`, `latestRootWholeRegressionVerified=false`로 범위를 구분한다.

초기 맵 준비가 멈춘 실행, 초기 시각 보정 전 포즈 지연, NPC BeginPlay 등장 몽타주 덮어쓰기, 후속 NPC 비행 경로의 관찰자 충돌, 지연 생성 fixture의 AttributeSet 등록 누락을 포함한 중간 실패는 최종 성공 증거에서 제외했다. 관련 로그와 분류는 작업 폴더/최종 감사에 보존한다. 최종 DLL의 native 검사 단계 오류는 0이다. 네트워크 여섯 실행도 완료 후 추가 2초까지 fatal/assert/ensure/Net·Iris·BP 실행 오류가 없다. Iris 기동 시 모듈 추가 로드 경고와 기존 누락 자산/태그 경고까지 0이라는 뜻은 아니다.

### 28.5 검증 범위와 후속

공유 NPC 파일은 다른 작업의 기여도 변경을 보존하고 추가한 함수/선언만 분리 프로젝트와 대조한다. 다른 작업과의 편집 충돌을 줄이기 위해 이번 최종 결과는 이 계획 문서와 전용 `Saved/CombatAirborneLateJoin` 증거에 기록한다. 공용 로드맵과 레이드 아키텍처의 기존 진행 목록은 이번 단계에서 갱신하지 않는다.

Source·Config·Content/Blueprints를 복사한 분리 프로젝트에서 검사하며, 그 밖의 Content와 Plugins는 공유 프로젝트에 연결되어 있다. 사용자 에디터가 사용하는 루트 DLL을 교체하지 않는다. 루트에서는 다른 세션의 소스·자산·DLL 변경이 계속되므로 분리 런타임 결과를 최신 루트 전체 회귀로 간주하지 않는다.

NullRHI/한 PC의 서버와 두 클라이언트, 개발용 승인 피해 입력과 관측용 기간/중력 조건에 한정한다. 실제 입력 장치·전체 무기 적중 창·모든 운영 공격/몬스터·급경사와 이동 발판·재관련성·여러 PC/4인·Steam·Shipping과 최종 화면 픽셀/전용 연출은 후속이다. 전송 연결과 맵 로딩을 피격 전에 완료한 뒤 활성 단계 중 실제 게임 참가를 검증하므로, 콜드 로딩을 포함한 임의 시점 접속 전체를 보장하지 않는다.

## 29. Guardian 두 번째 근접 공격의 에어본 데이터 — 2026-10-29

### 29.1 범위와 실제 호출 경로

공유 DLL은 다른 담당의 UI 검증에서 읽기 고정 중이므로 최신 모듈 통합 대신 독립된 공격 데이터 적용을 진행했다. 조율 표에 `guardian-melee2-airborne-20261029`로 단일 시작 데이터와 전용 도구/출력을 예약했다. C++·GA/캐릭터 BP·공유 DLL·GUI/MCP·다른 담당 에셋은 수정하지 않는다.

Guardian CDO의 `CharacterStartUpData`는 기존 `DA_Guardian`을 가리키고, 해당 데이터는 `GA_Guardian_Melee1`과 `GA_Guardian_Melee2`를 이미 부여한다. 두 클래스는 서로의 자식이 아니며, 기존 `AttackKnockbacks`는 비어 있었다. 두 번째 공격은 기존 `GA_NPC_MeleeAttack_Base.HandleApplyDamage`의 공용 피해 적용을 사용한다. `NPCStartUpData.BuildAttackAbilitySpec`이 클래스별 설정을 부여 spec에 고정하고, `RPGGameplayAbility.GetGrantedKnockback`이 이를 읽는다. `BP_ApplyEffectSpecHandleToTarget`은 승인 피해 뒤 `TryRouteApprovedHitReaction` → `CombatControlComponent.ApplyApprovedDamageKnockback`으로 전달한다. 높이가 양수이면 기존 에어본/실제 착지/다운/기상 경로를 사용한다. 새 GA나 공격 BP를 만들지 않는다.

### 29.2 적용할 초기값과 보존 조건

`Content/Blueprints/Character/NPC/Gruntling/Guardian/DA_Guardian.uasset`의 `AttackKnockbacks[GA_Guardian_Melee2]` 한 항목만 추가한다.

| 설정 | 초기값 |
|---|---:|
| 수평 거리 | 240cm |
| 높이 | 180cm |
| 실제 착지 뒤 다운 | 0.6초 |
| 자동 기상/행동 복구 단계 | 0.8초 |
| 수평 전용 Duration 필드 | 기본 0.4초 유지; 높이가 있으므로 실제 비행 시간은 기존 중력 계산 사용 |

이 값은 첫 조정값이며 최종 난이도/사용감 승인 수치가 아니다. 두 공격의 원래 클래스/부여 순서, 기존 첫 공격 경직 0.35초, 피해와 시작 GE, 반응 정책·스턴·둔화·화상·면역 설정을 보존한다. 이미 생성된 NPC의 spec은 고정되므로 새 설정을 읽은 뒤 새로 부여되는 공격부터 적용한다.

도구는 `Scripts/Editor/ConfigureGuardianMelee2Airborne.py`다. 기본 실행은 읽기 전용 검사이며, `-RPGGuardianMelee2Apply`에서만 저장한다. 저장 전 확보한 `Saved/CombatGuardianMelee2/Before.json`의 데이터/파일 해시와 대조하고 원본을 `Backups/DA_Guardian-Before.uasset`에 보관한다. 기존/상속 설정이나 대상 데이터의 다른 변경이 있으면 덮어쓰지 않는다. `-RPGGuardianMelee2Reload`와 함께 실행하면 새 프로세스에서 기존 값 유지와 추가 저장 0을 확인한다.

### 29.3 확인 결과

기존 정상 모듈 `AA6C9CB08578832686CEB5CBC474A43CF7DB76F0121A335E78EF17AD00127810`을 읽는 독립 commandlet 두 번에서 적용/새 프로세스 재로드 종료 0을 확인했다. 최초 대상 저장은 1회이며, 새 프로세스의 같은 작성 함수 재호출은 저장 0회·추가 변경 불필요·dirty=false였다. 시작 데이터의 대상 외 10개 속성과 다른 넉백 항목, 첫 공격 경직 0.35초 및 두 원래 부여를 보존했다.

대조한 입력 46개 중 `DA_Guardian.uasset` 한 개만 바뀌었고, 나머지 45개(원래 공격 BP/공용 부모·캐릭터·반응 프로필·무기·관련 소스·정상 DLL)는 동일하다. 대상 에셋의 최종 SHA256은 `7B8767D7722BC58EEC3D8762CF67CF94DE5286C9E562A95175DC84C122B807FC`다. 증거는 `Saved/CombatGuardianMelee2/Verification.proof.json`, 원본 상태는 `Before.json`, 적용/재로드 상세는 `ApplyResult.json`/`ReloadResult.json` 및 각 프로세스 로그에 보존했다.

이번에는 새 C++/게임 모듈 빌드나 기존 에디터/PIE 조작을 하지 않았다. 실제 두 번째 공격의 자연 입력/적중·물리 이동·멀티플레이 검증은 이번 데이터 저장 결과와 구분한다. 이전 94개/6조합 성공을 새 운영 공격의 실전 검증으로 계산하지 않는다. 후속은 통합 실행 환경에서 두 번째 공격의 경고·적중 타이밍과 이 초기값의 사용감을 확인하는 것이다.

## 30. Guardian 원래 두 번째 공격의 적중·물리 회복 확인 — 2026-10-29

### 30.1 검사 방법

조율 표의 `guardian-melee2-live-20261029` 범위에서 `Scripts/Editor/VerifyGuardianMelee2PIE.py`를 추가했다. 독립 숨김 편집기의 `/Engine/Maps/Entry`에서 실제 PIE를 시작한다. 해당 PIE의 GameMode 인스턴스에 시험용 기본 캐릭터/컨트롤러를 지정하고, 엔진의 `CreatePlayer`로 로컬 플레이어를 생성한다. 대상은 기존 `RPGStunTestCharacter`와 `RPGPlayerController`이며, 기존 AttributeSet의 초기 체력을 1000으로 설정한다. 실제 플레이어와 같은 이동 보안 조건을 갖추도록 기존 `RPGSecurityValidationComponent`도 해당 월드에만 추가한다. 바닥은 실행 중 생성한 정적 메시다.

공격자는 원래 `BP_Gruntling_Guardian`이다. 기존 시작 데이터/무기 부여가 완료되고 등장 몽타주가 끝난 뒤 AI의 이동/의사결정을 멈추고, 이미 부여된 원래 `GA_Guardian_Melee2`를 ASC에서 한 번 활성화한다. NullRHI에서도 원래 무기 소켓/노티파이가 갱신되도록 이 인스턴스의 메시 포즈/본 갱신을 켠다. 피해·적중 이벤트 주입, 대체 어빌리티 부여, 무기 충돌 수동 활성화, 공격 몽타주/속도·데이터 변경은 없다. 원래 몽타주와 무기 충돌 → `NPCCombatComponent.OnHitTargetActor` → 공용 GA 피해 처리 → 승인 피격의 실제 연결을 관측한다.

명시적 `-RPGGuardianMelee2Live` 플래그와 Entry 월드가 필요하며 기존 PIE가 있으면 시작하지 않는다. 독립 편집기의 `-ExecCmds="t.MaxFPS 30,py C:/UE5/Project_RPG/Scripts/Editor/VerifyGuardianMelee2PIE.py"`로 실행한다. 이번 실행의 드라이버는 `Saved/CombatGuardianMelee2Live/Run.ps1`이며, 고유 UserDir/로그와 자체 PID만 사용한다. 완료 시 자기 PIE/편집기만 종료한다. 공유 GUI/MCP·다른 담당의 빌드/검사/포트·DLL은 조작하지 않는다.

### 30.2 결과와 근거

최종 실행 `PIE-77f08c66e9c34560afbba566277f1254`는 편집기/러너 종료 0이며 아래 결과를 통과했다.

| 관측 | 결과 |
|---|---|
| 원래 두 번째 GA 활성화 / 공격 몽타주 | 승인 / `AM_Guardian_MeleeAttack` 관측 |
| 무기 적중 피해 | 체력 1000 → 995; 치명타 여부에 따라 피해량은 달라질 수 있음 |
| 적중 당시 부여 spec 설정 | 거리 240cm / 높이 180cm / 다운 0.6초 / 기상 0.8초 유지 |
| 실제 이동 | 최대 상승 179.98cm / 수평 변위 238.96cm / 원래 바닥 높이에 착지 |
| 순서 | Airborne → 실제 착지·Downed → GettingUp → None / `Completed` |
| 이동 보안 | 위반 0 / 회복 뒤 이동 허용 상태 해제 |

약 0.05~0.08초 간격의 샘플에서 공격 후 약 1.03초에 공중, 2.16초에 다운, 2.76초에 기상, 3.56초에 복구를 관측했다. 이 샘플 시각을 엔진의 정확한 노티파이 발생 시각으로 해석하지 않는다. 검사 대상/보안 조건을 준비하는 과정의 Python API·초기 캐릭터·보안 누락·로컬 플레이어 미연결 실패는 각 이전 실행 로그에 보존하고 최종 성공에 포함하지 않았다. 게임 코드나 에셋 수정으로 우회하지 않았다.

실제로 로드한 정상 공유 모듈은 `AA6C9CB08578832686CEB5CBC474A43CF7DB76F0121A335E78EF17AD00127810`이다. 원래 입력 46개(해당 DLL/Guardian 데이터·GA/무기·관련 소스 등)의 전후 해시 변동은 0이며, 검증 스크립트도 실행 때 기록한 SHA256과 일치한다. 기존 기동 `LogAutomationTest: Error: Condition failed` 15줄은 별도 집계하고 그 밖의 Error 및 fatal/assert/ensure는 0이다. 기존 누락 에셋/Blueprint 경고까지 0이라는 뜻은 아니다. 증거는 `Saved/CombatGuardianMelee2Live/Verification.proof.json`, 해당 run의 `Result.json`·`Process.proof.json`·`Editor.log`다. 같은 폴더의 `Audit.py <run 폴더명>`으로 입력/결과/로그를 다시 대조할 수 있다.

### 30.3 범위와 다음 연결

운영 NPC의 원래 공격 GA·몽타주·무기 적중과 공용 피격 이동/회복을 Standalone PIE 한 사례에서 확인했다. 대상은 스켈레탈 메시가 없는 기존 네이티브 시험 캐릭터이며 NullRHI이므로 실제 플레이어의 피격 포즈·화면 경고/사용감·입력 조작을 확인한 결과는 아니다. AI가 패턴을 고르는 과정과 멀티플레이 복제도 이 실행의 검증 범위에 포함하지 않는다.

공유 모듈을 새로 빌드/교체하지 않았으며, 앞선 분리 DLL의 후발 참가 수정이나 다른 담당의 최신 소스가 정상 공유 DLL에 통합됐다고 주장하지 않는다. 다음은 조율된 통합 모듈에서 실제 플레이어를 대상으로 이 운영 공격의 서버/클라이언트 적중·피격 포즈를 연결해 확인하는 것이다. 완료된 native 94개/여섯 후발 참가 검사를 이번 단계에서 반복하지 않았다.

## 31. 원래 공격의 Iris 검증 연결과 소유 클라이언트 상승 오차 — 2026-10-29

### 31.1 연결한 범위

`RPGGuardianMelee2NetworkProbe.cpp`와 `Scripts/Network/Run-GuardianMelee2NetworkTest.ps1`를 추가했다. 개발 플래그 `-RPGGuardianMelee2Network`에서만 독립 Entry 월드의 GameMode 인스턴스/시험 장면을 준비한다. 실제 `BP_Player` 두 명과 원래 `BP_Gruntling_Guardian`을 사용하고, 기존 Melee2 부여를 ASC에서 한 번 활성화한다. 원래 몽타주/노티파이/무기 충돌이 승인 피해와 에어본을 발생시킨다. 피해/이벤트 주입·대체 GA 부여·공격 데이터/중력/단계 기간 변경은 없다. 체력은 ASC의 기존 AttributeSet을 조회한다.

서버와 피격자/관찰자 클라이언트의 실제 Iris 및 NetDriver 지연 80~160ms/손실 5%를 검사한다. 두 클라이언트에서 플레이어/공격자·반응 프로필이 준비된 뒤 공격한다. 파일은 공격 전 준비 확인과 관측 결과 회수에만 쓰며, 전투 상태는 실제 복제로 수신한다. 활성 비행을 기다리며 서버를 멈추거나 효과 시간을 연장하지 않는다. 관찰자 캐릭터는 NPC 생성 전에 기존 이동 보안 허용을 사용해 피격자에게서 1200cm 떨어뜨리고, 검사 후 체력 보존·보안 위반/남은 허용 상태 0을 대조한다. 원래 피격자의 이동이나 체력은 직접 설정하지 않는다.

### 31.2 최종 관측: 상태·피해·복귀는 일치, 비행 높이는 실패

관측 run은 `Saved/CombatGuardianMelee2Network/3338fdc74a8247a0bbdad5d9b05df87c`다. 실제 세 프로세스의 관측 완료/종료 코드는 모두 0이다. 이는 아래 비행 궤적 조건의 통과를 뜻하지 않는다.

| 항목 | 서버 | 피격자 클라이언트 | 관찰자 클라이언트 |
|---|---:|---:|---:|
| 피격자 체력 | 100 → 90 | 100 → 90 | 100 → 90 |
| 공중/다운/기상/복구 상태 | 모두 관측 | 모두 관측 | 모두 관측 |
| 해당 단계의 반응 몽타주 인스턴스 | Dedicated Server에서 재생 제외 | 3단계 모두 관측 | 3단계 모두 관측 |
| 원래 위치 대비 최고 상승 | 179.98cm | **292.37cm** | 180.00cm |
| 최종 위치의 서버 대비 오차 | 기준 | 0.00cm | 약 0.003cm |
| 피격자의 이동 보안 위반 / 회복 후 허용 잔여 | 0 / 없음 | 0 / 없음 | 0 / 없음 |

관찰자 자신의 체력은 100으로 보존됐다. 원래 spec의 거리 240cm/높이 180cm/다운 0.6초/기상 0.8초도 적중 단계에서 유지됐다. 따라서 원래 공격의 피해/상태/단계별 포즈와 최종 복귀 연결은 확인했지만, **소유 클라이언트 비행 궤적 검증은 실패**다. 최종 위치만 맞으면 통과하던 검사에 최고 상승의 서버 대비 차이 35cm 상한을 추가했다. 이는 눈에 띄는 경로 불일치를 잡는 개발 검사 기준이며 최종 사용감 허용치가 아니다. 해당 관측은 112.39cm 차이로 거절된다.

현재 러너는 이 조건을 통과해야 전체 `Network.proof.json`을 생성한다. `-AuditRun <관측 폴더>`는 UE를 다시 띄우지 않고 같은 원본 관측에 기준을 적용하며, 이번 자료에서는 예상대로 종료 1/`MotionContract.proof.json.passed=false`다. 이전 느슨한 조건의 성공 기록은 `PhasePoseAndLanding.proof.json`으로 이름을 구분해 보존했다. 최종 `Saved/CombatGuardianMelee2Network/Verification.proof.json`도 `passed=false`이며, 상태/포즈/피해/최종 위치 하위 결과와 비행 실패를 별도 필드로 기록한다. `Audit.py <run ID>`의 종료 0은 입력/기록 무결성 대조의 성공이고 전투 검사의 성공 코드가 아니다.

### 31.3 분리 빌드와 보존한 증거

이전 본인 분리 빌드 `AJ_1a95d0`의 소스/PCH/객체와 고정 import library를 기준으로 새 검사 CPP 한 개를 컴파일했다. 링크 객체는 별도 사본을 사용하고 기존 분리 DLL/공유 Intermediate에는 쓰지 않는다. 새 RuntimeProject의 Blueprint/Config를 복사해 고정하고, Guardian 시작 데이터만 이번 원래 `7B8767D7…B807FC` 사본으로 반영했다. 원래 플레이어/Guardian/GA/공용 공격 부모/무기 에셋은 루트와 같은 해시임을 확인했다. 그 밖의 Content/Plugins는 공유 경로에 연결돼 있어 프로젝트 전체를 고정한 사본으로 표현하지 않는다.

최종 실행 DLL은 `2CEDCCEA0697F7E0FA067F05845000CB3D04FE77531D854F802DC3D01BD7663B`다. 고정 입력 2498개와 실제 로드 경로/해시를 전후 대조했다. 컴파일/링크는 종료 0이며 기존 객체의 PCH 디버그 정보에 관한 LNK4206 경고 360개가 남는다. 실행 중 관측 완료 이전 Error/fatal/assert/ensure는 0이다. 전체 종료 신호 후 서버가 먼저 닫혀 관찰자에 기록된 Host closed/ConnectionLost Error 2줄은 별도 집계했다. 누락 에셋/태그·기존 콘텐츠 경고도 보존한다.

최초 준비 대기는 Iris가 동적 NPC 인스턴스 이름을 다르게 생성했는데 검사에서 서버 이름을 찾은 문제였다. 실제 클래스와 PlayerState ID로 대상을 식별하도록 검사만 수정했다. 중간 실행 `9a1098ad…`의 소유 클라이언트 상승은 약 207cm였지만 관찰자 자신에게도 피해가 기록돼 최종 분리 관측으로 사용하지 않는다. 초기 컴파일의 보호된 프로필 접근 실패와 준비 만료·중간 로그/결과도 전용 작업 폴더에 보존한다. 확인되지 않은 게임 코드 원인으로 단정하지 않는다.

루트 DLL `AA6C9CB0…127810`·기존 C++/GA/맵/무기·UI 에셋과 다른 담당의 프로세스/빌드/검사는 유지했다. 기존 native 94개/여섯 후발 참가 검사나 전체 회귀는 반복하지 않았다.

### 31.4 다음 수정과 완료 기준

다음은 `RPGCombatAirborne.cpp`의 `OnRep_AirborneState`/`ApplyAirborneMotion`/소스 채택과 소유 클라이언트의 CMC 보정·저장 이동 재생 순서를 대조하는 작업이다. 현재 자료로 중복 루트 모션, 시작 시각/진행도, 예측 재생 중 어느 것이 원인인지 확정할 수 없다. 다른 담당과 파일/자원을 다시 조율하고 소유 클라이언트의 소스 ID·진행 시간·보정 전후 높이를 관측해 원인을 좁힌다. 수치·중력·타이머·검사 상한을 완화해 맞추지 않는다.

수정 후 같은 원래 Melee2 한 사례에서 소유자 상승 오차까지 통과하고, 피해/포즈/실제 착지/회복·관찰자 보존·이동 보안 정리를 함께 유지해야 한다. 그 다음 변경이 영향을 주는 기존 집중 검사만 선택한다. 이번 증거는 NullRHI/한 PC/직접 IP/개발용 GameMode 장면이며 실제 입력·픽셀 화면·AI 패턴 선택·티켓/백엔드 입장·최신 정상 공유 모듈 통합/여러 PC·Steam/Shipping의 완료를 뜻하지 않는다.

## 32. 소유 클라이언트 에어본 이동 시계 수정 — 2026-10-29

### 32.1 확인한 원인과 수정

31절의 과상승을 새 분리 DLL에서 다시 재현했다. 수정 전 진단 run `3d7bd06378814dbdab97af821511c986`은 서버 약 179.64cm, 소유자 약 298.83cm였으며 같은 35cm 기준에 실패했다. 루트 모션 ID/시작 타임스탬프/진행 시간·수직 속도·피격 단계 경과를 함께 기록했다.

소유자는 CMC 위치 보정으로 이미 높이 약 265cm(바닥 기준 캐릭터 중심 약 98cm)에 올라간 상태에서, 늦은 GameState 시각으로 계산한 약 0.057초의 점프 진행을 새로 적용했다. 서버 루트 모션 시작 이동 타임스탬프는 약 27.408초인데 소유자 소스는 약 27.741초였다. 이후 CMC 보정이 진행을 다시 맞추기 전까지 상승이 더해졌다. 착지 뒤에도 피격 상태의 늦은 시각 때문에 같은 실행의 소스를 다시 만드는 기록이 있었다. 최종 위치 수렴만으로 이 문제를 검출할 수 없었다.

`FRPGAirborneState.MovementStartClientTime`을 추가해 서버가 실제 CMC 루트 모션에 사용한 이동 시작 타임스탬프를 같은 복제 스냅샷으로 보낸다. 원격 플레이어의 소유 클라이언트는 `CurrentTimeStamp - MovementStartClientTime`으로 이동 진행과 소스 시작 시간을 맞춘다. `RPGCombatAirborne.cpp`의 공통 `OwnerMoveAge`를 생성/재개와 활성 판정 모두에 사용하므로, GameState 시각 보정만으로 유효한 소스를 만료시키거나 중복 생성하지 않는다.

서버·로컬 권위 플레이어·일반 NPC/관찰자 이동과 단계 수명/포즈의 서버 시각 기준은 유지한다. 소유자 이동 시계가 없거나 초기화·오래된 값·비정상 값으로 유효 범위를 벗어나면 기존 월드 시각으로 돌아간다. 거리/높이/중력·다운/기상 기간·검사 상한을 바꾸지 않았다. 새로운 공격 GA나 HealthComponent도 추가하지 않았다.

### 32.2 최종 검증

최종 게임 코드의 run은 `89b2a167e8b74b77885d7ea1e0822c58`이다. 실제 `BP_Player`/원래 Guardian Melee2·몽타주/무기 적중, 서버와 피격자/관찰자 두 클라이언트의 Iris 및 지연 80~160ms/손실 5%를 사용했다.

| 관측 | 서버 | 소유자 | 관찰자 |
|---|---:|---:|---:|
| 피격자 체력 | 100 → 90 | 100 → 90 | 100 → 90 |
| 최고 상승 | 179.98cm | 179.64cm | 179.99cm |
| 최고 상승의 서버 대비 오차 | 기준 | **0.35cm** | 약 0.004cm |
| 공중/다운/기상/복구 상태 | 모두 관측 | 모두 관측 | 모두 관측 |
| 단계별 반응 몽타주 | 서버 재생 제외 | 3단계 모두 관측 | 3단계 모두 관측 |

최종 위치 수렴, 관찰자 자신의 체력 100 보존, 실제 착지/다운/기상/Completed, 피격자의 보안 위반 0과 회복 뒤 이동 허용 해제를 함께 통과했다. 세 UE/러너 종료는 모두 0이다. 앞선 기본 수정 DLL의 두 독립 실행도 같은 기준에 통과했으며 소유자 상승 오차는 약 7.91cm와 0cm였다. 해당 단계의 DLL/`FirstFix.proof.json`은 별도 보존해 최종 모듈과 구분한다.

신규 `OwnerMoveClockBootstrap`을 포함한 에어본 집중 native **10/10·종료 0**이다. 늦은 초기 GameState, 이미 진행된 CMC 이동 시계, 앞선 GameState 시각에서 기존 소스 유지/중복 생성 방지, 이동 타임스탬프 초기화의 제한된 fallback을 검사한다. 기존 실제 착지/회복·면역/교체/정화·천장/벽/낙하 만료·수명/보안·재진입·경사/단차·시각/포즈 재동기화도 통과했다. 초기 추가 테스트는 제거 예정인 이전 소스를 중복으로 세어 9/10으로 실패했으며, 같은 실행의 유효 소스만 세도록 테스트를 수정한 뒤 최종 10/10을 확인했다. 해당 실패를 삭제하거나 게임 동작 성공으로 표현하지 않는다.

최종 네트워크 DLL은 `A556DC7C06FF3C6EACD3436D3D17F58B6114A23900CE5B3EA4E8BF6B687E5E24`였으며 검증 당시 `UnrealEditor-FinalGameplay.dll`에 보관했다. 최종 native DLL은 `5BE64B919B50119785BF05566BE4B3695C4C1286B8ACC2468BA87A272E5A2A4F`였다. 두 모듈 사이에는 위 테스트의 집계 수정만 있다. 링크 입력 전체의 전후 해시를 대조해 `RPGAirborneTests.obj` 한 개 외 게임/생성 코드/라이브러리 입력이 동일함을 확인했으므로, 테스트 수정 때문에 통과한 게임 네트워크 실행을 반복하지 않았다. 두 DLL의 해시/증거 범위를 합쳐 하나의 실행으로 주장하지 않는다. 두 검증 바이너리와 입력 사본은 33절의 자동 폐기 지침에 따라 정리했으며, 이 해시는 당시 실행의 증거다.

새 UHT 출력/생성 코드와 실제 컴파일 객체·DLL은 전부 `Saved/CombatAirborneOwnerFix/`에 분리했다. 이전 AJ/네트워크 실패 DLL·결과와 공유 Intermediate에는 쓰지 않았다. 고정 입력 2624개의 변동은 0이다. UHT/컴파일/링크는 성공했고 기존 객체의 PCH 디버그 정보 LNK4206 경고 360개는 남는다. 최초 자동 선택 컴파일러의 PCH 버전 거절은 `CompilerSelectionFailure.log`에 보존하고 기존 PCH와 같은 MSVC 14.44를 명시해 해결했다.

최종 증거는 `Saved/CombatAirborneOwnerFix/Verification.proof.json`이며 `passed=true`다. 원시 관측은 위 네트워크 run, 최종 native 결과는 `Native-1eeb6f6cec654f089e4b6072a2cd58e7`에 남긴다. 검증 당시 `Audit.py <native 폴더> <network run ID>`로 고정 입력·실제 DLL/로그·이동 시작 시계 수신·한 테스트 객체만의 차이를 대조했다. 33절의 폐기 후 같은 입력/DLL의 재감사는 불가능하며, 새 실행에는 새 빌드와 별도 증거가 필요하다. 기동 콘텐츠 경고와 native의 기존 기동 오류는 관측 결과와 구분하며, 링크 경고/프로젝트 전체 오류가 0이라고 주장하지 않는다.

### 32.3 당시 재실행 명령과 남은 통합

다음은 검증 당시의 실행 명령이다. 33절의 정리로 아래 RuntimeProject는 폐기했다. 재실행할 때에는 조율된 새 빌드의 디렉터리를 `-RuntimeProject`에 명시한다.

```powershell
Scripts/Network/Run-GuardianMelee2NetworkTest.ps1 `
  -RuntimeProject C:/UE5/Project_RPG/Saved/CombatAirborneOwnerFix/RuntimeProject `
  -OutputRoot C:/UE5/Project_RPG/Saved/CombatAirborneOwnerFix -Port 19107
```

정상 공유 DLL `AA6C9CB0…127810`과 GUI/MCP·다른 담당의 파일/프로세스/검사는 유지했다. 이번 수정은 소스와 분리 검증까지 완료했으며, UHT 스키마를 포함한 정상 모듈 통합은 다음 조율된 빌드에 반영해야 한다. 기존 94개/여섯 후발 참가 전체 검사는 반복하지 않았다. 실제 입력/픽셀 화면·모든 공격/높이/이동 지형·실제 장시간 이동 타임스탬프 초기화·여러 PC/Steam/Shipping까지 검증한 결과는 아니다.

## 33. 검증 산출물 폐기와 네트워크 러너의 자동 정리 — 2026-10-30

### 33.1 이번 작업과 공유 빌드 경계

재개 시 로드맵 12절의 최신 조율 표와 승인 적중→공용 Knockback/Airborne 호출, 복제 상태/이동 시작 시계 코드를 다시 확인했다. 공유 UHT/UBT·Intermediate는 `action-progress-state-20261029` 담당자가 예약 중이어서 정상 모듈 빌드에 착수하지 않았다. 독립 작업으로 사용자 지시의 검증 종료 후 임시 산출물 자동 폐기를 적용했다. 에어본 게임 소스 네 개는 32절의 최종 검증 해시와 같다.

### 33.2 정리 결과와 남긴 증거

본인 `Saved/CombatAirborneOwnerFix/`, `Saved/CombatGuardianMelee2Network/`의 빌드/생성 코드·소스/실행 프로젝트 사본, 분리 DLL과 UserDir를 폐기했다. 본인 Freeze.py/Run-Native.ps1이 만든 `Saved/CombatAirborneLateJoin/`의 일곱 `Snapshot-*`, 전용 DDC와 `Test-*/User`, 별칭 `C:/UE5/AJ_1a95d0`도 정리했다. 원본 게임 Source/Content/Config/Plugins/Binaries/Intermediate·공유 DDC와 다른 담당자의 경로는 정리 대상에 넣지 않았다.

- 삭제한 일반 파일: **19,278개 / 13,054,024,882 bytes(12.1575GiB)**. 이는 파일 길이 합계이며 디스크 할당량 측정값은 아니다.
- 링크만 해제한 항목: **296개**. Content/Plugins·이전 DDC의 연결 대상을 따라가지 않았다.
- 보존 대상 **362개**의 최종 SHA256 일치. 이전 성공/실패 proof·소스/모듈 해시·명령과 필요한 로그는 남겼다. 후발 참가의 스냅샷 안에 있던 작은 실행 결과/로그는 별도 `LateJoinEvidence/`로 옮겨 보존했다.
- 정리 범위의 남은 대형 예외/예약 대상: 없음. 원본 DLL은 여전히 `AA6C9CB08578832686CEB5CBC474A43CF7DB76F0121A335E78EF17AD00127810`이다.

첫 정리는 임시 소스 사본의 읽기 전용 디렉터리 삭제에서 중단됐다. 실패 계획/결과를 남기고, 일반 임시 디렉터리의 읽기 전용 속성만 해제하는 처리를 보완한 뒤 남은 경로를 다시 계획·정리했다. 원래 전체 목록의 잔여 0과 보존 파일 해시를 대조해 중단 직전 일부 삭제까지 포함한 최종 용량을 집계했다. 최종 결과는 `Saved/Maintenance/CombatVerificationCleanup-20261030/Completion.proof.json`의 `passed=true`이며, 최초 실패는 `InitialCleanupFailure.json`에 남겼다.

28~32절의 검증 결과는 당시 실행의 증거다. **해당 스냅샷·DLL·고정 입력은 폐기했으므로 같은 바이너리의 재실행이나 입력 재감사는 불가능하다.** 기존 proof를 새 실행 성공으로 갱신하지 않았다. 재검증에는 새로 조율한 빌드와 새로운 실행 기록이 필요하다.

### 33.3 유지할 러너 동작과 검증

`Scripts/Network/Run-GuardianMelee2NetworkTest.ps1`는 보관된 옛 RuntimeProject를 기본 선택하지 않고, 실행할 빌드의 디렉터리를 `-RuntimeProject`로 받는다. `Invocation.json`에 모듈 해시/실행 인자와 임시 경로를 남긴다. 정상 종료와 중간 실패 모두 finally에서 자기 프로세스의 종료를 확인한 뒤 해당 run의 `server-User`/`owner-User`/`observer-User`를 지우고 `Cleanup.proof.json`을 기록한다. 강제 종료가 필요하면 PID의 실제 명령줄에서 같은 run ID와 전용 플래그를 확인한다. 소유/종료 확인이 실패하면 파일을 보존하고 정리 실패를 보고한다.

공통 처리는 `Scripts/Network/GuardianMelee2Artifacts.ps1`에 두었다. 삭제 경로가 명시한 임시 루트의 자식인지 확인하며, 링크를 거친 부모 경로와 루트 자체의 삭제는 거절한다. 디렉터리 링크는 대상 열거 없이 해제한다. 호출자가 넘긴 RuntimeProject나 원본 모듈은 러너의 소유 산출물이 아니므로 삭제하지 않는다. 빌드용 임시 사본의 수명은 그 사본을 만든 작업이 관리한다.

`Scripts/Tests/Test-GuardianMelee2Artifacts.ps1`의 **18개 검사항목이 통과**했다. 경로 이탈/루트/링크 부모 거절, 읽기 전용 임시 폴더 삭제와 Junction 대상 보존, 실제 작은 PowerShell 자식 프로세스를 이용한 정상/시작 실패 후 종료·UserDir 정리, 결과/호출자 모듈/환경변수 보존을 확인했다. 로그와 게임 결과만 모형이며 UE·UDP·게임 빌드/실행은 사용하지 않았다. fixture도 종료 후 폐기했다. 초기 가짜 프로세스가 러너의 실제 Process 형식과 맞지 않은 시험 실패를 보존하고 실제 자식 프로세스 방식으로 보완했으며, 게임 검증 실패와 혼동하지 않는다. 결과는 같은 정리 폴더의 `RunnerLifecycle.proof.json`에 있다. 세 유지 스크립트의 PowerShell 구문 검사도 통과했다.

다음은 공유 예약 종료를 확인한 뒤 최신 프로젝트 소스와 새 UHT 스키마로 정상 모듈을 빌드하는 것이다. 그 새 모듈에서 영향 범위의 에어본 집중 검사와 원래 Guardian Melee2의 소유자/관찰자 검증을 연결한다. 이번 정리·러너 검증이 정상 모듈 통합이나 새 게임 네트워크 실행의 완료를 뜻하지 않는다.

## 34. 정상 통합용 에어본 집중 검증 러너 — 2026-10-30

### 이번 변경

재개 시 공유 UHT/UBT·Intermediate의 `action-progress-state-20261029` 예약이 유지되고 있음을 확인했다. 정상 통합 빌드는 시작하지 않았으며, 이전 Saved 경로/DLL에 의존하던 에어본 집중 검증을 유지할 스크립트로 옮겼다. 게임 소스·에셋·다른 담당의 파일/프로세스와 공유 DLL은 변경하지 않았다.

- `Scripts/Tests/Run-AirborneAutomationTest.ps1`: 지정 프로젝트의 Config/DLL/module descriptor를 작은 자체 RuntimeProject로 복사하고, 전용 UserDir에서 에어본 검사를 실행한다. Content/Plugins는 공유 링크이므로 프로젝트 전체의 고정 사본으로 표현하지 않는다. 선택 입력·모듈의 전후 SHA256, 관측한 실제 로드 DLL 경로/해시, 프로세스 종료 코드와 명령을 기록한다. 선택한 DLL이 빌드 결과와 같은지 제한하려면 `-ExpectedModuleSHA256`을 사용한다. 이 해시만으로 DLL이 최신 소스에서 빌드됐다고 증명하지는 않는다.
- `Scripts/Tests/AirborneAutomationReport.ps1`: `OwnerMoveClockBootstrap`을 포함한 필수 10개 이름을 관리한다. JSON 요약·각 검사 결과/오류 수·발견 로그·각 완료 로그와 종료 코드가 모두 맞아야 통과한다. 누락/중복/추가·미실행·실패·검사 이후 Error/fatal/assert/ensure를 거절한다. 검사 발견 이전 Error는 별도로 집계한다.
- `Scripts/Tests/Test-AirborneAutomationReport.ps1`: 소스에 선언된 실제 10개 이름과 필수 목록을 대조하고, 성공/실패 보고서의 판정 및 실행 전 모듈 해시 거절을 검사한다.

러너는 성공/실패 finally에서 같은 GUID/검사 명령으로 소유가 확인된 자기 프로세스의 종료를 확인한 뒤 Temp의 DLL·RuntimeProject/UserDir/캐시를 폐기한다. 경로/링크 처리는 33절의 기존 helper를 읽기 전용으로 재사용한다. 소유/종료 확인이 실패하면 Temp를 보존하고 실패를 기록한다. 결과에는 삭제 용량과 Temp 잔여 여부를 남긴다.

### 이번 검증과 한계

최종 **러너/보고서 검사항목 20개 통과**, 세 PS 파일의 구문 검사 통과다. 빈 보고서, 소유자 이동 시계 검사 누락, 중복/미실행/실패, JSON과 로그의 불일치, 비정상 종료 등을 거절했다. 보존된 실제 32절의 10개 성공 보고서와 로그도 읽기 전용으로 감사해 기존 기동 Error 15개/검사 이후 0을 확인했고, 원본 해시를 보존했다. 가짜 DLL에 잘못된 기대 해시를 주면 UE 실행 전에 거절하고 실패 증거/호출자 모듈을 보존하며 Temp를 남기지 않는다.

이것은 새 UE 실행이 아니다. **정상 모듈 통합·새 모듈의 native 10개 실행·실제 모듈 로드 관측/실행 중 종료·전체 live 러너 경로는 이번에 검증하지 않았다.** 소스/모듈 해시의 안정성은 전체 모듈 빌드 출처의 보장이 아니며, 실제 네트워크 검증은 별도 러너로 수행한다. 런타임 프로세스 종료/정리 처리의 실제 UE 경로도 첫 조율된 실행에서 확인해야 한다.

최종 결과는 `Saved/Tests/AirborneAutomation/Implementation.proof.json`의 `passed=true`다. 마지막 20개 판정은 `RunnerChecks-cf96c066bedd4b0893de0e1d14010834/RunnerChecks.proof.json`에 있다. 두 도구 검사에서 임시 fixture 총 **11,840 bytes**를 폐기했으며 대형 예외/임시 잔여는 없다. 작은 결과와 스크립트 해시만 남겼다. 공유 DLL은 계속 `AA6C9CB0…127810`이며 에어본 소스 네 개도 32절의 해시와 같다.

### 통합 후 실행 순서

공유 빌드 담당의 종료/인계를 확인하고 최신 소스의 정상 UHT/모듈 빌드를 완료한 뒤, 새 모듈과 검증 입력을 예약하고 다음을 실행한다. 빌드는 이 러너가 수행하지 않는다.

```powershell
Scripts/Tests/Run-AirborneAutomationTest.ps1 `
  -ProjectFile C:/UE5/Project_RPG/Project_RPG.uproject
```

기본 출력은 `Saved/Tests/AirborneAutomation/<GUID>/`의 Invocation/Report/Native.log/Verification.proof다. 새 실행의 실제 10개 결과와 자동 정리를 통과한 뒤 33절의 네트워크 러너를 새 모듈에 연결한다. 기존 94개/여섯 후발 참가 검사를 이번 도구 변경으로 반복하지 않았다.

## 35. 정상 Editor 모듈 통합과 실제 공격 재검증 완료 — 2026-10-30

### 35.1 공유 빌드와 정상 출력

HUD 담당의 공유 UHT/UBT·Intermediate 예약 해제와 정상 DLL 설치 기록을 확인했다. 등록 재조회 사이 던전·파티의 `online-editor-integration-20261030`와 빌드 예약이 겹쳐 양쪽 모두 실행 전에 중단했다. 그 담당의 **airborne 우선 진행·공유 예약 해제** 인계를 확인하고 본인 범위를 다시 예약한 뒤 한 번의 정상 빌드와 에어본 검증을 순차 수행했다. 다른 담당의 예약/프로세스를 임의로 해제·종료하지 않았다.

`Project_RPGEditor Win64 Development` 정상 빌드는 **종료 0**이다. 이미 생성/컴파일된 최신 입력을 사용해 CPP 재컴파일 0·링크 6개·타깃 메타데이터를 갱신했고, 컴파일/링크 경고 0이다. 이번에 UHT/CPP를 새로 생성·컴파일했다고 표현하지 않는다. 정상 설치 DLL은 이전 `4BCAB2FB…7B764E7`에서 다음 해시로 갱신됐다.

```text
Binaries/Win64/UnrealEditor-Project_RPG.dll
C428B66FA0BC1A164ACDCD86F6D590278D2BBC80F8F2BDC4BF408AA8981070ED
```

선택 입력은 Source·Config·Blueprints 및 프로젝트/검증 스크립트 **1,438개**이며 빌드 전부터 native/네트워크 종료 후까지 SHA256 변경 0이다. 게임 소스/에셋이나 유지할 러너를 이번 작업에서 수정하지 않았다. 다른 Content/Plugins 전체를 물리적으로 복사·해시한 프로젝트라고 주장하지 않는다. 정상 Binaries/Intermediate·종속 모듈 출력과 DLL/PDB는 유지할 최종 빌드 출력이다.

### 35.2 같은 정상 DLL의 집중 native 10개

34절의 유지 러너를 실제 실행했다. 새 정상 DLL과 같은 해시인 전용 RuntimeProject 사본이 실제 프로세스에 로드됐음을 관측했고, 필수 **10/10 Success·UE 종료 0**을 확인했다. `OwnerMoveClockBootstrap`, 초기/보정 시각·포즈 재동기화, 실제 착지/회복, 면역/교체/정화, 천장/벽/낙하 만료, 수명/보안·재진입, 경사/단차 사례가 포함된다. JSON 요약과 각 완료 로그의 이름/결과도 일치한다.

기동 Error 15줄은 기존 초기 조건 기록이며 검사 발견 이후 Error는 0이다. cold start를 기다린 뒤 실제 Queue Empty/종료 코드와 결과를 확인했으며, 시간 경과만으로 통과시키지 않았다. RuntimeProject/DLL/UserDir/캐시의 자동 정리까지 통과했다.

실행: `Saved/Tests/CombatNormalIntegration-20261030/Native/de25a2f81c0942d5961cc04729d8f2aa/`.

### 35.3 원래 Guardian Melee2의 실제 Iris 공격

같은 정상 설치 DLL로 실제 `BP_Player` 두 명과 원래 Guardian GA·몽타주·무기 충돌을 실행했다. 서버/소유자/관찰자의 실제 프로세스에서 **원본 Binaries DLL 경로와 위 해시**를 모두 관측했다. 피해 주입·대체 GA 부여·공격 수치/중력/단계 기간 변경 없이 Iris 지연 **80~160ms·패킷 손실 5%**를 적용했다.

| 관측 | 서버 | 피격자 클라이언트 | 관찰자 클라이언트 |
|---|---:|---:|---:|
| 피격자 체력 | 100 → 95 | 100 → 95 | 100 → 95 |
| 최고 상승 | 179.98cm | 179.64cm | 179.89cm |
| 서버 대비 최고점 오차 | 기준 | **0.346cm** | **0.090cm** |
| 공중/다운/기상/복구 | 모두 관측 | 모두 관측 | 모두 관측 |
| 단계별 반응 포즈 | 서버 재생 제외 | 3단계 모두 관측 | 3단계 모두 관측 |

서버 대비 최고 상승 오차 35cm 기준을 그대로 통과했다. 같은 이동 시작 타임스탬프 `26.685691833496094`가 세 역할의 공중 단계 샘플에 기록돼, 이동 시계 필드가 정상 모듈의 복제로 전달된 것도 확인했다. 최종 위치/체력 수렴·실제 착지/다운/기상/Completed·이동 보안 위반 0·회복 후 허용 해제를 확인했다. 관찰자 자신의 체력은 100으로 보존됐다. 세 UE/러너 종료는 모두 0이며, 각 로그의 관측 완료 전/후 Error도 모두 0이다.

이번 체력 결과는 현재 정상 모듈/현재 콘텐츠의 100→95 관측이다. 32절의 이전 분리 모듈에서 관측한 100→90을 이번 실행의 값으로 재사용하지 않는다.

실행: `Saved/Tests/CombatNormalIntegration-20261030/Network/8a63f52a7f544fa29a9efb2a23309520/`. `LoadedModules.proof.json`은 세 실제 로드 경로/해시, `MotionContract.proof.json`은 최고점 조건을 기록한다.

### 35.4 최종 감사·정리·인계

최종 결과는 `Saved/Tests/CombatNormalIntegration-20261030/Verification.proof.json`의 **passed=true**이며, `DetailedObservation.proof.json`에 복제 이동 시계·오류 구간·정리 상태를 별도로 남겼다. 본인 실행기의 조율표 조회는 담당 열과 다른 행의 인계 참조를 구분하도록 보완했고, 최종 집계의 PowerShell 속성 개수 조회도 보완했다. 실행 전 차단/집계 실패 기록은 남겼으며 게임 입력/DLL 변경이 없어 이미 성공한 UE 검사를 반복하지 않았다.

자체 임시 사본/UserDir/캐시·중복 콘솔 로그 **155,857,906 bytes(148.6377MiB)**를 폐기했고 남은 대형 예외/임시 경로는 없다. 작은 결과·명령·입력/모듈 해시·필수 로그를 보존했다. native의 RuntimeProject 사본은 폐기했으므로 그 사본 재실행/고정 입력 재감사는 불가능하다. 원본 정상 DLL은 최종 결과물로 유지했다. 이후 다른 담당의 빌드가 들어오면 이 해시는 이번 검증 시점의 증거로 구분해야 한다.

빌드·고정 DLL/Source/Config/Content/Plugins 입력·UDP19107 예약을 해제했다. 던전·파티 담당은 이 정상 DLL과 자신의 선택 입력을 새로 고정해 로비 Back/결과 탐색 2개를 이어서 확인할 수 있다. 그 검사는 이번 에어본 통과 항목에 포함하지 않는다.

이번 결과는 한 PC·NullRHI·Entry 시험 장면/직접 IP의 정상 모듈 검증이다. GPU/실물 입력·AI 패턴 선택·티켓/백엔드/Steam·여러 PC·Shipping, 이동 발판과 모든 지형/공격 조합, 실제 장시간 이동 타임스탬프 초기화는 별도다. 기존 전체 94개/여섯 후발 참가 조합은 이번에 반복하지 않았다.

## 36. 이동 발판의 정상 운반과 에어본 회복 경계 수정 — 2026-10-31

### 36.1 재현한 문제

35절의 정상 모듈 통합 뒤, `online-editor-integration-20261030`의 native/감사 종료와 전체 예약 해제를 확인하고 소스/빌드·검증 입력을 예약했다. 다른 담당의 게임/백엔드 소스나 에셋을 변경하지 않았다.

기존 테스트 월드에 엔진 `UInterpToMovementComponent`가 실제로 움직이는 수평 발판을 구성했다. 발판 속도는 600cm/s이며, 캐릭터 위치/단계/효과 시간을 직접 주입하지 않고 공용 승인 적중→루트 모션→실제 충돌/CMC 착지→다운/기상을 실행했다. 60Hz에서는 회복이 완료됐지만 **15Hz에서는 착지 후 `ForeignMovement`로 중단돼 GettingUp에 도달하지 못했다.** 발판 소실 시 낙하 유지와 실제 외부 텔레포트의 중단 사례는 수정 전에도 통과했다.

수정 전 `Baseline`의 세 검사는 2 Success/1 Fail이다. Carry 검사는 60Hz에서 착지 순간의 상대 좌표를 너무 일찍 고정해 발판 한 프레임(10cm) 차이도 실패로 셌다. 이 시험 가정과 15Hz의 실제 조기 종료를 구분했다. 최종 시험은 Landed 이후 첫 완전한 접지 갱신에서 상대 위치를 고정하며 이후 지속적인 드리프트를 검사한다. 기존 거리 상한을 늘려 통과시키지 않았다.

### 36.2 공용 코드 수정

엔진 `PerformMovement`는 based movement를 적용한 뒤 `OldLocation`을 저장하고 외부 이동 갱신 delegate를 호출한다. 기존 `OnAirborneMovementUpdated`는 지난 프레임의 월드 위치와 이 `OldLocation`을 직접 비교했다. 15Hz의 정상 발판 운반 40cm가 고정 불연속 상한 25cm를 넘어 외부 이동으로 오인됐다.

`RPGCombatAirborne.cpp`에서 CMC의 `bTeleportedSinceLastUpdate`가 false일 때는 `GetLastUpdateLocation()`을 이전 갱신 기준으로 사용한다. CMC가 외부 텔레포트를 감지한 경우에는 기존 `OldLocation` 기준을 유지한다. **25cm 불연속 상한·프레임 내부 이동량 상한·실제 낙하/착지·GAS 수명/면역·이동 보안 승인 정책은 바꾸지 않았다.** 추가 상태 관리자나 대상별 구현, 복제 필드를 만들지 않았으며 공용 컴포넌트 한 곳만 수정했다.

유지할 소스 변경은 `Private/Component/Combat/RPGCombatAirborne.cpp`와 `Private/Tests/RPGAirborneTests.cpp` 두 개다. 후자는 기존 월드 fixture를 재사용해 다음 별도 이름 공간을 추가했다. 기존 `ProjectRPG.Combat.Airborne.*` 10개 이름과 영구 러너는 그대로다.

- `ProjectRPG.Combat.AirbornePlatform.CarryThroughRecovery`: 15/60Hz에서 실제 발판 착지→다운→기상→Completed, 이동 소스 종료·액션 게이트·접지 후 발판 기준 위치 유지.
- `ProjectRPG.Combat.AirbornePlatform.BaseLossKeepsFalling`: 기상 중 발판 충돌 소실 시 UnsupportedMovement로 종료하며 빈 공간에 walking을 복구하지 않음.
- `ProjectRPG.Combat.AirbornePlatform.ForeignTeleportStillEndsRecovery`: 다운 중 캐릭터 자체의 300cm 외부 이동은 계속 ForeignMovement로 종료.

### 36.3 최종 정상 모듈 검증

기능 수정 DLL의 새 3개와 기존 10개가 각각 통과한 뒤, 새 테스트의 UE 5.8 폐기 예정 `GetMovementBase()` 호출을 `GetMovementBaseObject()`로 정리했다. 최종 정상 빌드는 CPP 1개/링크 2개·종료 0·컴파일/링크 경고 0이며, 앞선 기능 수정 빌드는 CPP 2개를 컴파일했다. 정상 설치된 최종 DLL 해시는 다음과 같다.

```text
85E3990397EE711C19ADFC9E73AA43801640B99075BEBFF9337FD374017BE0F3
```

이 **같은 최종 DLL 해시의 실제 로드 사본**에서 13개를 함께 실행해 모두 `Success`·UE 종료 0을 확인했다. UE 요약은 **12개 무경고 성공 + 1개 경고 포함 성공**이다. 새 이동 발판 3개는 모두 무경고다. 기존 `FutureClockAndOwnedPoseResync`에는 `AM_Guardian_Spawn`의 `JumpToSectionName Cilmb ... failed` 경고 1건이 기록됐으며, 무경고 13/13이라고 표현하지 않는다. 해당 등장 몽타주/섹션 에셋은 이번 범위에서 수정하지 않았다. 기동 Error 15줄과 검사 발견 이후 Error 0도 구분했다.

최종 Carry 관측은 60Hz에서 발판 1390cm·15Hz에서 1440cm 이동, 둘 다 실제 착지/기상/Completed와 접지 기준 이후 상대 드리프트 **0cm**다. 발판 소실·진짜 텔레포트와 기존 천장/벽/낙하·면역/정화·수명/시각·소유 이동 시계 회귀도 최종 13개에 포함된다.

최종 입력 1,433개는 빌드→native 종료 뒤 SHA256 변경 0이다. 수정 전/후 입력 차이도 위 두 CPP뿐이다. 결과는 `Saved/Tests/CombatMovingPlatform-20261030/Verification.proof.json`의 `passed=true`이며, 최종 보고서는 `Final/Report/index.json`, 실패 재현은 `Baseline/Report/index.json`에 남겼다. `Fixed`의 기능 수정 DLL과 `Final`의 테스트 API 정리 DLL을 같은 바이너리로 합쳐 기록하지 않았다.

### 36.4 정리와 남은 범위

성공뿐 아니라 최초 실패 실행도 종료 후 자기 Temp의 DLL/RuntimeProject·UserDir/캐시와 중복 콘솔 로그를 폐기했다. 누적 **622,407,723 bytes(593.5743MiB)** 정리·임시 잔여/대형 예외 0이다. 정상 Binaries/Intermediate·최종 DLL/PDB와 작은 결과·명령·입력/모듈 해시·필수 로그는 유지했다. 폐기한 실행 사본/이전 DLL은 재실행·고정 입력 재감사가 불가능하며 해시는 당시 증거다.

이번 fixture는 컨트롤러 없는 네이티브 캐릭터에 서버 물리를 실행한 NullRHI 검사다. 공용 전투 컴포넌트의 발판 운반/회복 경계를 확인했으며, **실제 플레이어의 이동 발판 보안 검증·발판 Iris 복제·GPU/실물 입력은 확인하지 않았다.** 회전/물리 시뮬레이션 발판·발판 전환/복잡한 충돌도 후속이다. 35절의 실제 Guardian Iris 결과는 당시 C428 DLL의 기록이며 이번 85E DLL의 새 네트워크 실행으로 확대하지 않는다. 기존 광역 94개/후발 참가 6조합도 반복하지 않았다. 본인 공유 빌드/소스·입력 예약을 해제했다.

## 37. 이동 발판의 플레이어 보안 샘플 분리 — 2026-10-31

### 37.1 재현과 시험 준비의 실패 구분

36절 후속으로 실제 `APlayerController`/`APlayerState`가 소유한 네이티브 캐릭터와 운영 `URPGSecurityValidationComponent`를 연결했다. 기본 정책과 생성 유예 2초를 유지하고 3초 뒤부터 시험했다. 수평 발판은 600cm/s이며 캐릭터는 승인 에어본→실제 CMC 착지→다운/기상을 수행한다.

수정 전 `BaselinePlayerState`에서는 60/15Hz 모두 정상 회복을 마쳤지만, 자발적 이동 속도가 0인 다운/기상 중 정상 운반을 속도 위반으로 각각 6번 기록했다. 반대로 발판 +10cm/frame과 캐릭터 무단 이동 -10cm/frame을 상쇄하면 월드 이동 0이 돼 상대 속도 600cm/s를 검출하지 못했다. 두 문제 모두 월드 좌표 차이만 보안 수학에 전달한 데서 발생했다.

착수 당시 소비 담당자의 Source 예약과 겹쳤을 때 재확인이 부족해 최초 `Baseline` 빌드가 먼저 실행됐다. 이후 입력 검사에서 해당 담당의 서버 관측기 두 파일 변경을 발견해 native 실행을 차단했고 `InitialBuildHandoffInvalidated.json`에 무효 범위를 남겼다. 소비 담당자의 추가 편집 중단/우선 인계 뒤 최신 입력으로 다시 빌드했다. 그 두 파일을 본인이 편집하거나 소비 실연결을 검증하지 않았다.

`BaselineHandoff`의 첫 fixture에는 PlayerState가 없어 UE 5.8의 `IsPlayerControlled()`가 false였다. 보안 샘플 자체가 실행되지 않은 시험 실패로 구분하며 게임 결함의 증거로 사용하지 않는다. PlayerState를 추가한 `BaselinePlayerState`만 위 두 현상의 재현 근거다. 또 최초 텔레포트 시험은 착지의 정상 `EndMovementAuthorization(..., true)` 기준 초기화 직후 이동해 새 기준에 흡수됐다. 최종 시험은 착지 후 13프레임을 기다려 기준을 확립한다. **기준 초기화 직후의 관측 공백을 이번 변경으로 해소하거나 검증했다고 주장하지 않는다.**

### 37.2 구현 계약

대상별 전투 구현을 추가하지 않고 기존 공용 이동/보안 컴포넌트 사이에 서버 운반량만 전달한다.

1. `URPGCharacterMovementComponent::UpdateBasedMovement`의 `Super` 호출 전후 실제 UpdatedComponent 위치 차이를 서버 누적 운반량으로 기록한다. 예측한 발판 변위나 최대 속도를 허용량으로 더하지 않는다. 캐릭터 자체의 외부 이동은 이 구간 이전에 일어나므로 운반량에 포함되지 않는다.
2. 기록 대상은 같은 월드의 접지한 플레이어와 동적 primitive 발판이다. 발판의 attachment/owner 경로에 Pawn/Controller가 있거나 attachment 액터에 서버 권한이 없으면 보정하지 않는다. 플레이어 소유 운반체는 기존 월드 좌표 검사를 유지한다. 모든 서버 권한 액터가 안전하다는 일반 보증은 아니며, 향후 원격 조작 운반체는 별도 신뢰 계약이 필요하다.
3. 보안 샘플은 직전 샘플 이후 실제 서버 운반량을 직전 비교 위치에 더해 상대 이동을 계산한다. 저장한 원래 샘플은 월드 좌표로 유지한다. 이동 컴포넌트가 바뀌면 같은 누적계로 간주하지 않으며 기준 초기화 시 약한 참조/직전 누적값도 초기화한다.
4. 기존 `FRPGSecurityValidationMath`·허용치·연속 위반 횟수·위험도/조치·생성 유예·이동 승인 정책을 유지한다. 새 RPC/복제 값·클라이언트 신고 운반량·발판 탑승 면제/추가 허용 창은 없다.

변경 소스는 `Public/Private/Component/RPGCharacterMovementComponent.{h,cpp}`, `Public/Private/Component/RPGSecurityValidationComponent.{h,cpp}`, `Private/Tests/RPGAirborneTests.cpp`의 5개다. 수정 전/후 선택 입력 1,435개를 대조해 이 5개 외 변경 0을 확인했다. 기존 에어본 10개/발판 3개 이름·전투 컴포넌트/에셋·타 담당의 소스를 유지한다.

### 37.3 분리 모듈 빌드와 집중 실행

사용자 에디터 PID 2584가 열려 정상 DLL 교체를 멈췄다. 에디터를 종료하지 않고 `-NoLink` 빌드 후 동일 링크 응답의 OUT/PDB만 자기 TempBuild로 바꿔 분리 링크했다. 컴파일 49개·UBT 종료 0·분리 링크 종료 0이다. 기존 `RPGGameplayAbility.cpp`의 `NonInstanced` 폐기 예정 경고 1건이 남아 있으며 무경고 빌드로 표시하지 않는다. 정상 설치 DLL 해시 `9968DDA8…EA32034`는 유지됐다.

분리 DLL의 SHA256은 다음과 같으며 실제 테스트 프로세스의 RuntimeProject 로드 경로/해시를 관측했다.

```text
DDBCA4774421584FC138EFF13F9142C22BC3B1161732B16C6117398878248A7C
```

같은 분리 모듈에서 `ProjectRPG.Combat.Airborne` 17개를 함께 실행해 **17/17 Success·UE 종료 0**이다. 16개는 무경고이고 기존 `FutureClockAndOwnedPoseResync`의 Guardian 등장 몽타주 섹션 경고 1건은 남았다. 기동 Error 15줄과 검사 발견 이후 Error 0을 구분한다. 새 4개는 모두 무경고 성공이며 거절 시험의 예상 보안 경고도 정확한 종류/횟수를 대조한다.

| 새 시험 | 관측 |
|---|---|
| `AirbornePlatformPlayer.CarryPreservesSecurity` | 60/15Hz 모두 실제 착지·다운·기상·Completed, 착지 후 이동 승인 없음, 강제 이동 입력 중 위반 0/위험도 0 |
| `AirbornePlatformPlayer.RejectsForeignTeleport` | 기준 확립 뒤 무단 2,500cm 이동은 MovementDiscontinuity로 보고, 전투도 ForeignMovement 종료 |
| `AirbornePlatformPlayer.RejectsRelativeCounterMotion` | 월드 이동 0cm인 상쇄 이동도 상대 속도 600cm/s를 검출, 위반 0→1 |
| `AirbornePlatformPlayer.PlayerOwnedBaseKeepsRawChecks` | Controller 소유 발판은 운반량 0을 유지하고 기존 속도 검사로 위반 보고 |

입력 1,435개의 빌드/실행 전후 해시가 동일하다. 상세 결과는 `Saved/Tests/PlatformPlayerSecurity-20261031/Fixed/{Build,Native}.proof.json`, `Report/index.json`과 최상위 `Verification.proof.json`에 남긴다. 최초 빌드/fixture 실패와 실제 결함 재현을 최종 성공으로 덮어쓰지 않았다.

### 37.4 정상 반영 대기와 후속 범위

현재 **소스 수정·분리 모듈 집중 검증 완료, 정상 설치 DLL 반영 대기**다. 열린 사용자 에디터의 기존 모듈에는 이번 수정이 없다. 정상 반영은 에디터 종료 확인 후 최신 Source/공유 자원을 새로 예약하고 입력을 다시 고정해 정상 링크해야 한다. 다른 담당의 후속 변경이 들어오면 이번 분리 모듈의 성공과 구분한다.

자기 Temp/TempBuild의 DLL/PDB·RuntimeProject/UserDir/캐시는 실패/성공 각 실행 종료 후 폐기했고 중복 콘솔 로그/이전 시험 초안도 정리했다. 누적 **678,809,795 bytes(647.3635MiB)** 폐기·임시 잔여/대형 예외 0이다. 정상 Binaries/Intermediate와 작은 결과·명령·입력/모듈 해시·최소 로그는 유지한다. 분리 DLL/실행 사본은 이미 폐기했으므로 위 해시는 당시 실행 증거이며 동일 사본의 재실행/고정 입력 재감사는 불가능하다. 본인 Source/공유 빌드·검증 입력 예약을 모두 해제했으며 소비/훈련 담당의 후속 작업은 최신 입력과 자원을 새로 예약해 순차 진행한다.

이 시험은 NullRHI의 로컬 권한 PlayerController/PlayerState fixture다. **실제 BP_Player 원격 이동 RPC·Iris 발판 복제/보정·다인 지연/손실·회전/물리 발판·복잡한 충돌/발판 전환·GPU/실물 입력은 후속**이다. 다음 전투 검증은 실제 BP_Player 소유자/관찰자의 이동 발판 운반과 상대 이동 보안 연결이며, 35절의 이전 Guardian 네트워크 성공을 이번 분리 모듈의 네트워크 성공으로 계산하지 않는다.

후속: 실제 BP_Player의 수평 발판 운반/피격 복귀는 38절에서 원격 경로의 추가 결함을 수정하고 분리 모듈로 확인했다. 37절의 로컬 fixture 결과만으로 원격 검증을 대신하지 않았으며 정상 설치 반영은 여전히 에디터 종료 대기다.

## 38. 원격 플레이어의 이동 갱신 사이 운반과 실제 발판 복귀 수정 — 2026-10-31

### 38.1 실제 멀티플레이에서 추가 재현한 결함

37절 후속 착수 시 정상 DLL `9968DDA8…EA32034`와 사용자 에디터 PID 2584가 그대로여서 정상 교체를 수행하지 않았다. 최신 조율표에서 다른 담당의 Source/공유 빌드 예약이 없고 HUD/훈련 실행은 독립 사본·스크립트인 것을 확인해 새 범위를 등록했다. 소비/훈련의 원본 적용 대기 패치와 다른 담당의 소스를 수정하지 않았다.

실제 `BP_Player` 두 명·원래 Guardian GA/몽타주/무기 충돌을 기존 Entry 전용 네트워크 관측기에 연결했다. 서버의 `UInterpToMovementComponent`가 원래 적중의 Airborne 관측 뒤 수평 발판을 600cm/s로 움직이며 발판 transform/mobility/mesh는 Iris로 복제한다. 공격 수치/중력/단계 기간·피해/단계/포즈를 주입하거나 대체 GA를 부여하지 않는다. 피격 소유자에게는 다운/기상 중 강제 이동 입력을 넣어 기존 CMC 차단도 확인한다.

`Platform` 실행에서 서버는 실제 착지 뒤 **Downed 2샘플만 관측하고 ForeignMovement(5)로 종료**했다. 당시 발판 기준 X는 -454.21755cm로 유지됐는데 GettingUp에 도달하지 못했다. 로그/JSON은 `Platform/MovingPlatform/dac7c3f76e47450685bb966eced7167c/`에 보존했다. 이는 보안 수학의 속도 위반이 아닌 공용 에어본의 외부 이동 오인이다.

UE 5.8 `TickComponent`의 서버 원격 플레이어 분기는 클라이언트 이동 패킷 사이에도 `MaybeUpdateBasedMovement`/`MaybeSaveBaseLocation`으로 운반한다. 이 경로는 `PerformMovement` 밖이므로 `LastUpdateLocation`은 이전 RPC의 위치에 남는다. 다음 `PerformMovement`의 `bTeleportedSinceLastUpdate`가 true가 돼 36절의 분기가 정상 운반을 외부 이동으로 읽었다. 로컬 권한 fixture의 17개 성공으로 이 원격 경로까지 보증할 수 없음을 실연결에서 확인했다.

### 38.2 수정 방법과 보존한 경계

기존 CMC의 실제 서버 운반 누적량을 에어본 판정에도 재사용한다. 실행 시작에 누적량을 고정하고 각 이동 갱신에서는 `OldLocation - (현재 운반 누적량 - 직전 누적량)`을 직전 에어본 위치와 비교한다. `OldLocation`은 현재 이동의 based movement 이후 값이므로 RPC 사이와 RPC 내부의 실제 운반을 함께 제거한다. 비교 후 위치/누적량 기준을 같이 갱신한다.

서버 운반 측정은 플레이어에만 제한하던 조건을 제거해 권한 있는 NPC에도 같은 경로를 제공한다. 기존의 같은 월드/접지/동적 primitive·attachment/owner 신뢰 조건과 Pawn/Controller 소유 운반체 제외는 유지한다. 보안 컴포넌트의 기존 비교/허용치·승인 창·연속 위반/위험도 정책은 변경하지 않는다. **25cm 불연속 상한과 프레임 내부 이동 상한도 유지**하며, CMC 텔레포트 플래그에 의존하지 않는다. 추가 서버 기준은 비복제 private FVector 1개이고 새 RPC/클라이언트 운반 신고·대상별 상태 관리자는 없다.

유지할 변경은 Source 5개와 네트워크 러너 1개다.

- 운영: `Private/Component/Combat/RPGCombatAirborne.cpp`, `Public/Component/Combat/RPGCombatControlComponent.h`, `Private/Component/RPGCharacterMovementComponent.cpp`.
- 검사: `Private/Tests/RPGAirborneTests.cpp`, `Private/Tests/RPGGuardianMelee2NetworkProbe.cpp`.
- 실행: `Scripts/Network/Run-GuardianMelee2NetworkTest.ps1`의 `-MovingPlatform`과 세 실제 로드 모듈/역할별 명령 기록. 움직이는 발판은 각 역할의 완료 시각에도 계속 전진하므로 최종 월드 X 대신 발판 상대 좌표의 수렴을 검사한다.

Source 경로는 모두 `Source/Project_RPG` 아래다. 37절 최종 입력과 비교해 Source 변경은 이 5개뿐이며 에셋·Config/Plugins·타 담당 소스는 유지했다.

### 38.3 같은 최종 분리 DLL의 native 18개와 네트워크 2모드

최종 `CarryFix`의 `-NoLink` CPP 42개·분리 링크·타깃/링크 종료는 모두 0이다. 기존 `RPGGameplayAbility.cpp`의 NonInstanced 폐기 예정 경고 1건이 남아 무경고 빌드로 계산하지 않는다. 정상 DLL을 유지한 채 다음 분리 모듈을 만들었고, native와 네트워크 여섯 역할 프로세스에서 실제 RuntimeProject 로드 경로/해시를 확인했다.

```text
FC15411917BDD1DBA439C9A96BC1A0368B17D28D208085B7C5F815E5E0778BC7
```

같은 모듈의 native **18/18 Success·경고 0·UE 종료 0**이다. 기동 Error 15줄과 발견 이후 Error 0을 구분한다. 새 `AirbornePlatformPlayer.BetweenMoveCarryKeepsForeignChecks`는 실제 엔진 mover/based movement/base 저장을 두 차례 호출해 이동 갱신 사이 운반 60cm를 만들고, 회복 유지 뒤 추가 무단 35cm 이동의 ForeignMovement 종료를 확인한다. 기존 NPC 발판 3개/플레이어 보안 4개/시각·착지·면역·정화·수명 10개도 함께 통과했다. 이번에 과거 Guardian 등장 섹션 경고를 에셋 수정으로 해결했다고 주장하지 않는다.

실제 `BP_Player` 소유자/관찰자와 원래 Guardian 적중은 **Iris 지연 80~160ms·손실 5%**, 30FPS의 기본 바닥/이동 발판 두 모드 모두 통과했다. 각 서버/클라이언트의 단계 비트 15·클라이언트 반응 몽타주 비트 14·실제 착지/기상/Completed·최종 체력 수렴·관찰자 보존·보안 위반 0/이동 승인 해제를 확인했다. 여섯 UE 종료도 모두 0이다.

| 최종 관측 | 서버 | 피격 소유자 | 관찰자 |
|---|---:|---:|---:|
| 기본 바닥 피격자 체력 | 100→95 | 100→95 | 100→95 |
| 기본 바닥 최고 상승(cm) | 179.927 | 174.522 | 179.927 |
| 이동 발판 피격자 체력 | 100→90 | 100→90 | 100→90 |
| 이동 발판 최고 상승(cm) | 179.637 | 179.637 | 179.986 |
| 이동 발판 운반 거리(cm) | 1901.174 | 1901 | 1921 |
| 다운/기상 접지 샘플 | 18/24 | 16/26 | 18/24 |
| 회복 중 발판 상대 드리프트(cm) | **0** | **0** | **0** |

두 모드의 자연 적중 관측 피해량이 다르므로 100→95를 발판 실행 값으로 재사용하지 않는다. 발판 최고점 오차는 소유자 0cm/관찰자 0.3493cm로 기존 35cm 기준을 통과했다. 최종 발판 상대 X는 서버/소유자 -497.27916cm, 관찰자 -497.28125cm이며 Y는 모두 0이다. 서로 다른 완료 시각의 발판 전진 거리를 위치 불일치로 오인하지 않았다. 드리프트 상한 50cm/최종 상대 수렴 30cm 기준을 결과에 맞춰 늘리지 않았다.

### 38.4 실패·집계·정리와 남은 작업

초기 `First`는 클라이언트의 동적 액터 이름이 서버와 달라 발판 검색이 대기한 관측기 실패다. `FixtureFix`는 기본 바닥 통과 뒤 로컬 복제 설정을 클라이언트 식별 조건으로 쓰는 코드를 재검토해 측정 전에 중단했다. 이름/로컬 복제 설정 대신 이 Entry fixture에 유일한 Cube/scale로 식별하고 복제 도착을 기다리도록 수정했다. 발판 메시 확대 배율을 제거한 `InverseTransformPositionNoScale`로 실제 cm를 측정한다. 중단 시 정확한 run/명령/PID를 확인한 **자기 프로세스만** 종료했고 두 실행을 게임 성공으로 승격하지 않았다. 이후 `Platform`은 실제 운영 결함 재현, `CarryFix`만 최종 native/두 모드 성공이다.

최초 결과 집계는 완료 후 연결 종료 오류까지 같은 구간으로 세어 중단됐다. 원본 결과/로그·각 Complete와 러너의 `exit` 파일 UTC 시각을 재감사했으며 UE 실행을 반복하지 않았다. 최종 네트워크 여섯 로그의 **관측 시작→Complete 중 Error는 모두 0**이다. 다만 이동 발판 관찰자는 모든 관측 승인/exit 파일 생성 이후 서버 동시 종료 때문에 `Host closed the connection`/`ConnectionLost`의 LogNet Error 2줄이 남았다. 여섯 프로세스 종료 0과 별도로 보존하며 전체 네트워크 로그 무오류로 표현하지 않는다. `AggregationReview.json`과 최상위 proof에 원문/시각을 기록했다. 종료 프로토콜의 client-first 정리는 별도 러너 개선 후보다.

최종 선택 입력 **1,443개**가 컴파일/실행/집계 전후 동일하다. 결과는 `Saved/Tests/AirbornePlatformNetwork-20261031/Verification.proof.json`의 `passed=true`이며 최종 상세 자료는 `CarryFix/Native.proof.json`·`NativeReport/index.json`, `CarryFix/{StaticFloor,MovingPlatform}/<run>/`의 역할별 결과/모듈/명령/네트워크 proof다. 모든 실패/중단의 자기 TempBuild·DLL/PDB·RuntimeProject/UserDir·캐시·중복 콘솔 로그를 자동 폐기했고 누적 **1,052,346,660 bytes(1003.596MiB)** 정리·임시 잔여/예외 0이다. 원본 정상 Binaries/Intermediate·소스/유지 러너/에셋과 작은 증거는 유지했다. 분리 모듈은 폐기했으므로 위 해시는 당시 증거이며 같은 바이너리/입력 사본의 재실행은 불가능하다.

본인 Source/공유 UHT·UBT·Intermediate/입력·UDP19108 예약을 해제한다. **정상 설치 DLL은 기존 `9968DDA8…EA32034`이며 37·38절 수정은 아직 미반영**이다. 에디터 종료 확인 뒤 최신 입력/공유 자원을 다시 예약해 정상 링크하는 것이 우선 후속이다. 다른 담당의 정상 빌드가 먼저 반영하면 그 소스/객체/모듈 인계를 확인해 중복 작업을 피한다.

이번 성공은 한 PC·NullRHI·직접 IP/Entry 개발 fixture의 원격 BP_Player 이동 RPC와 수평 kinematic 발판이다. 네트워크 악성 상대 이동/텔레포트 공격, 회전/물리 시뮬레이션·발판 전환/복잡한 충돌, 원정 티켓/백엔드/Steam·GPU/실물 입력·Shipping은 별도다. 실제 AI 패턴 선택이나 모든 공격/지형 조합, 기존 광역 94개/후발 참가 전체를 새로 검증한 결과는 아니다.

## 39. 발판 포함 18개를 유지 러너에 연결 — 2026-10-31

### 39.1 선택 모드와 판정 계약

38절 후속 확인에서 정상 DLL은 기존 `9968DDA8…EA32034`이고 사용자 GUI2584는 열려 있었다. 소비 담당이 원본 Source2/공유 UHT·UBT·Intermediate를 사용해 패치 적용/NoLink를 진행 중이어서 정상 반영이나 새 공유 빌드를 시작하지 않았다. 독립된 기존 검사 스크립트 3개만 예약/수정했다. 기존 러너는 10개만 선택하므로, 별도 Saved 실행기에 있던 발판/플레이어 8개를 유지할 러너에서도 선택할 수 있게 연결했다.

| 실행 옵션 | 엔진 선택 문자열 | 필수 결과 |
|---|---|---|
| 기본 | `ProjectRPG.Combat.Airborne.` | 기존 10개 |
| `-IncludeMovingPlatforms` | `ProjectRPG.Combat.Airborne` | 기존10 + 발판3 + 플레이어5 = 18개 |

선택 문자열 끝의 마침표 유무는 명시적 계약이다. 필수 이름은 `Get-RPGAirborneAutomationTests`에서 관리하고 실행/보고서 검사 모두 같은 선택값을 사용한다. 다른 모드의 JSON/발견 로그를 섞거나 새 `BetweenMoveCarryKeepsForeignChecks`를 다른 사례로 바꾸어 총수만 맞춰도 거절한다. 각 필수 이름/성공·오류 수, JSON 요약, 정확한 선택/발견 수, 각 완료 로그, 프로세스 종료 코드를 함께 검사한다. 기존의 무경고 성공 정책·검사 발견 이전 Error 별도 집계·발견 이후 Error/fatal/assert/ensure 거절은 유지한다.

`Run-AirborneAutomationTest.ps1`은 선택한 모드/필수 이름을 정상 실행과 사전 실패의 Invocation에 모두 남긴다. 확장 모드에서는 공용 전투 헤더·이동/보안 컴포넌트 h/cpp도 선택 입력에 추가하며 두 모드 모두 실행기/보고서/정리 helper 해시를 기록한다. DLL 실제 로드 관측·선택 입력/실행 사본 전후 대조·자기 프로세스 소유 확인/finally 정리도 기존 경로를 사용한다. 이 선택 해시 목록은 전체 프로젝트의 빌드 입력 증명이나 새 DLL의 소스 provenance를 대신하지 않는다.

수정 파일은 `Scripts/Tests/{Run-AirborneAutomationTest,AirborneAutomationReport,Test-AirborneAutomationReport}.ps1`뿐이다. 게임 소스/설정/에셋·정상 모듈·공유 빌드 출력·다른 담당의 실행/예약은 수정하지 않았다.

### 39.2 도구 검사와 보존된 실제 결과 재감사

기본 모드 **25개**, 확장 모드 **27개**의 보고서/사전 거절 검사를 통과했다. 공통 사례가 두 모드에서 반복되므로 52개의 서로 다른 게임 테스트를 실행한 것으로 표현하지 않는다. 실제 소스의 선언과 10/18개 목록 일치, 누락/중복/대체·실패/미실행/오류·요약/완료/발견 불일치, 다른 모드로 감사하는 경우를 거절했다. 확장 모드에는 between-move 사례 대체와 플레이어 외부 텔레포트 실패도 포함했다. 가짜 모듈의 해시 불일치를 두 모드 모두 UE 시작 전에 거절하고 실패 Invocation의 모드/필수 목록과 정리를 확인했다. PS 구문/공백 확인도 통과했다.

35절 C428 모듈의 실제 10개 JSON/로그와 38절 FC154 모듈의 실제 18개 JSON/로그를 각각 **읽기 전용**으로 새 보고서 검사기에 통과시켰고 전후 SHA256이 같았다. 이는 보존된 실행 결과의 새 판정기 감사이며 **새 UE/native/네트워크 실행·현재 정상 DLL의 18개 성공이 아니다.** 해당 과거 검증 DLL은 폐기됐으므로 같은 바이너리를 재실행했다고 표현하지 않는다.

증거:

- 기본: `Saved/Tests/AirborneAutomation/RunnerChecks-47ad102a5a594ad2a6f3fe802a44ca25/RunnerChecks.proof.json`.
- 확장: `Saved/Tests/AirborneAutomation/RunnerChecks-b7688277c9f64b68bf823cc93d5e3e2b/RunnerChecks.proof.json`.
- 최종: `Saved/Tests/AirborneRunnerPlatform-20261031/Verification.proof.json`의 `passed=true`, 두 모드 검사/기존 결과 해시·스크립트3 해시·실행 없음/정상 반영 대기를 기록한다.

가짜 모듈/보고서/로그 등 자기 fixture **20,714 bytes**를 각 성공/실패 finally에서 폐기했다. 임시 잔여/예외 0이며 작은 proof/명령/해시만 남겼다. 대형 DLL/PDB/입력 사본·캐시는 만들지 않았고 원본 정상 출력과 다른 담당의 산출물은 유지했다. 본인 PS3/fixture 예약을 해제한다.

### 39.3 정상 반영 뒤 사용할 명령과 남은 경계

37·38절이 포함된 정상 DLL을 설치하고 공유 검증 입력을 새로 예약한 뒤 사용한다.

```powershell
& Scripts/Tests/Run-AirborneAutomationTest.ps1 -IncludeMovingPlatforms
```

해당 정상 빌드의 DLL로 제한하려면 `-ExpectedModuleSHA256`에 그 빌드 proof의 해시를 전달한다. 옵션을 생략하면 기존 10개다. 최신 옵션만 추가해 현재의 이전 DLL에 새 검사들이 생기는 것은 아니며, 필수 18개가 없는 모듈의 결과는 실패한다. `Test-AirborneAutomationReport.ps1 -IncludeMovingPlatforms -HistoricalReport ... -HistoricalLog ...`는 저장된 결과 감사용이고 새 게임 실행을 하지 않는다.

정상 DLL은 아직 `9968DDA8…EA32034`이며 37·38절 반영은 에디터 종료·다른 담당 공유 컴파일 종료/인계 후 별도 예약/링크가 필요하다. 이번 독립 도구 작업을 그 설치 완료로 계산하지 않는다. 네트워크 client-first 종료·악성 원격 이동/회전·물리 발판 등 38절의 미검증 경계도 그대로다.

## 40. 상승 측정 누락의 잘못된 네트워크 PASS 수정 — 2026-10-31

정상 설치를 훈련 담당이 예약한 동안 공유 소스/빌드와 겹치지 않는 결과 판정 오류를 재현했다. 기존 `Test-MotionContract`는 세 결과에서 `rise`가 모두 빠져도 PowerShell의 null 산술 변환으로 소유자/관찰자 오차를 각각 0으로 계산해 **passed=true**를 썼다. `Saved/Tests/GuardianMotionContract-20261031/Baseline.proof.json`에 누락된 세 상승 값과 잘못된 PASS를 보존했다. 게임 물리/복제 결함을 새로 재현한 결과는 아니다.

`Scripts/Network/Run-GuardianMelee2NetworkTest.ps1`의 해당 함수만 보완했다. 세 역할 각각 실제 boolean `success=true`와 유한한 JSON 숫자 `rise`를 요구한다. 기존 C++ `Complete()`가 이미 요구하던 최소 상승 50cm도 결과에서 확인한다. 누락/null·숫자처럼 보이는 문자열·boolean/배열·비유한 값·실패/손상 결과를 거절하며, 기존 **35cm 오차 상한과 경계값 통과는 유지**한다. 실패한 재감사는 오류 이유와 `passed=false`를 기록해 이전 PASS 파일을 남기지 않는다. 게임 Source/RPC/공격/판정 수치·모듈을 수정하지 않았다.

새 `Scripts/Tests/Test-GuardianMelee2MotionContract.ps1`의 **27개 판정 검사**가 통과했다. 세 역할 각각의 누락/null, 잘못된 값/결과/역할 파일, 35cm 경계 통과와 초과 거절, 이전 PASS의 실패 덮어쓰기를 확인했다. 38절의 실제 기본 바닥/발판 결과 3역할씩은 자기 Temp의 작은 사본으로 감사해 통과했으며 원본 SHA256은 전후 같다. 이는 과거 결과의 판정기 감사로 **새 UE/네트워크 실행이 아니다**. 새 정상 설치 모듈의 실연결은 41절에서 구분한다.

최종 증거는 `Saved/Tests/GuardianMotionContract-20261031/Implementation.proof.json`이며 작은 fixture/이전 결과 사본 **398,173 bytes**를 종료/실패 finally에서 폐기했다. 임시 잔여/대형 예외 0, 빌드/UE·포트/DB 사용 0이며 원본 DLL·훈련 담당의 공유 빌드/사본/프로세스는 유지했다. 작은 실패 재현·판정 결과·원본/스크립트 해시를 남기고 본인 PS2/fixture 예약을 해제한다.

## 41. 정상 설치 모듈의 전투 반영 확인

2026-10-31 야간~11-01에 **37·38절의 정상 설치 반영 대기를 해소**했다. HUD 예약 해제 뒤 훈련 결과/복귀 담당이 정상 게임 모듈을 링크/설치했으므로 본인은 중복 UBT/링크나 Source/에셋/원본 Binaries 교체를 하지 않았다. 해당 담당의 설치 입력, 이전에 검증한 전투 소스, 현재 소스 **8개 SHA256이 모두 같은 것**과 실제 설치 DLL을 대조했다. 본인 결과를 전체 Editor 타깃을 새로 빌드한 것으로 표현하지 않는다.

최종 설치 게임 모듈:

```text
Binaries/Win64/UnrealEditor-Project_RPG.dll
F1A202697D2FC64E36045B5B01C7B17FFC37D699A580CBB83A017242E1AC5654
```

### 41.1 설치 사본의 집중 native 18개

39절의 유지 러너 `-IncludeMovingPlatforms -ExpectedModuleSHA256 <설치 해시>`를 실제 실행했다. 이 **같은 F1 모듈 해시의 독립 RuntimeProject 사본**을 프로세스가 로드한 것을 관측했으며 필수 **18/18 Success·경고 0·UE 종료 0**과 JSON/발견/완료 로그 교차 판정을 통과했다. 기동 Error 15줄과 검사 발견 이후 Error 0을 구분한다. Source/Config/DLL은 본인 고정 입력으로 예약하지 않고 전후 대조해 일관된 사본을 검증했으며, 다른 담당의 공유 빌드·설치·실행을 막지 않았다.

실행: `Saved/Tests/AirborneNormalInstall-20261031/Native/751d44403a2146d8b3bb6b5c0fdfdb65/`.

최초 설치 `01232619…35E0F84`의 앞선 실행은 18개 모두 Success/UE0이지만 17무경고+기존 Guardian `JumpToSectionName Cilmb ... AM_Guardian_Spawn` 경고1이었다. 무경고 정책의 러너가 그 실행을 실패로 판정한 원본 proof를 보존했다. 이어 같은 012 해시로 네트워크 사본을 준비하던 시점에는 훈련 담당의 로비 fixture 보완 설치로 DLL이 바뀌어 **UE 시작 전에 차단**됐다. 이후 최종 F1 모듈을 별도로 고정해 native/네트워크를 모두 확인했다. 서로 다른 모듈 결과를 합쳐 단일 DLL 성공으로 만들지 않았다. 이번에 몽타주/섹션 에셋을 고쳤다고 주장하지 않으며 이전 경고도 삭제/숨김하지 않았다.

### 41.2 같은 설치 모듈의 실제 발판 Iris 적중

원본 DLL/Config/descriptor·실행 스크립트의 읽기 사본을 닫은 뒤 자기 RuntimeProject에서만 서버/소유자/관찰자를 실행했다. 원본은 다른 담당의 입력으로 유지하고 본인의 실행 사본만 고정했다. 실제 `BP_Player` 두 명과 원래 Guardian GA/몽타주/무기 충돌에 **Iris 지연 80~160ms·손실 5%**를 적용했다. 세 역할 모두 실제 로드한 DLL 경로/해시가 위 F1 사본임을 관측했다. 40절의 상승 결과 판정도 이 실행에 실제 적용됐다.

| 실제 발판 실행 | 서버 | 피격 소유자 | 관찰자 |
|---|---:|---:|---:|
| 피격자 체력 | 100→95 | 100→95 | 100→95 |
| 최고 상승(cm) | 176.7073 | 171.8976 | 179.9444 |
| 다운/기상 접지 샘플 | 19/24 | 11/24 | 10/23 |
| 회복 중 상대 드리프트(cm) | 5.68e-14 | 5.68e-14 | 0 |
| 최종 발판 상대 X(cm) | -462.95193 | -462.95193 | -462.95313 |

최고점 오차는 소유자 4.8097cm/관찰자 3.2371cm로 기존 35cm 기준을 통과했다. 상대 드리프트는 부동소수점 오차 수준이고 최종 상대 위치도 기존 30cm 수렴 기준 안이다. 공중/다운/기상/Completed, 클라이언트 세 반응 몽타주, 관찰자 자신의 체력100 보존·보안 위반0/이동 승인 해제, 세 UE 종료0을 확인했다. 이전 FC154 분리 모듈의 수치를 이번 F1 실행 값으로 재사용하지 않는다.

실행: `Saved/Tests/AirborneNormalInstall-20261031/Network-F1/Run/6c6549211b874b0f86cfa5e82da36edb/`.

관측 시작→Complete 중 세 로그 Error는 0이다. 모든 관측 승인/exit 파일 생성 이후 동시 종료 과정에서 소유자/관찰자 각각 `Host closed the connection`/`ConnectionLost` Error 2줄씩, **총4줄**이 남았다. 정확한 메시지와 UTC 시각을 승인 exit 파일과 대조해 별도 기록했으며 전체 로그 무오류로 표현하지 않는다. client-first 종료 개선은 여전히 후속이다.

### 41.3 정리·인계와 미검증 범위

최종 `Saved/Tests/AirborneNormalInstall-20261031/Verification.proof.json`은 **passed=true·normalIntegrationComplete=true**다. 소스8의 이전 검증/현재/설치 입력 해시, 동일 F1의 native18/실제 세 역할 모듈과 결과, 앞선 012의 strict 실패/사전 차단, 종료 연결 오류를 구분해 남겼다. 본인 게임 Source/에셋/원본 빌드 출력 쓰기와 게임 빌드는 0이다.

최초/최종 native와 네트워크의 자기 Temp DLL·RuntimeProject/UserDir·캐시/링크를 사용 해제 후 폐기했다. 누적 **327,488,576 bytes(312.3174MiB)** 정리·임시 잔여/대형 예외0이다. 40절의 작은 fixture까지 이번 재개의 자체 폐기 합계는 327,886,749 bytes(약312.7MiB)다. 정상 설치 DLL/PDB·Intermediate는 유지할 출력이며 삭제하지 않았다. 검증 사본은 폐기돼 동일 사본의 재실행/고정 입력 재감사는 불가능하다. 정상 파일도 후속 설치로 바뀔 수 있으므로 위 해시는 검증 시점의 설치 증거로 취급한다.

본인 독립 검사/PS·Temp/UDP19108 예약을 해제한다. 훈련 담당의 Source/설치·사본/프로세스 예약은 변경하지 않았다. **37~39절의 정상 DLL 대기는 이번 F1 기준으로 해소**됐으며 앞 절의 대기 문구는 당시 상태다. 회전/물리 발판·발판 전환·악성 원격 이동/텔레포트, GPU/실물 입력·여러 PC/Steam/Shipping·전체 회귀는 별도다. 훈련 결과/복귀 UI나 다른 담당 기능의 전체 완료까지 이번 전투 결과로 보증하지 않는다.

## 42. 수평 넉백의 정상 발판 운반 오인 수정 — 2026-11-01

### 42.1 독립 재현과 공유 자원 인계

41절의 에어본 정상 반영 이후 수평 넉백의 `OnKnockbackMovementUpdated`는 여전히 직전 위치와 based movement 이후 `OldLocation`을 직접 비교했다. HP/MP 담당이 공유 NoLink/소스 읽기를 사용 중이어서 원본을 고치지 않고, 소스/생성 헤더·링크 입력을 자기 Temp로 해시 확인해 복사한 뒤 새 재현 검사와 분리 모듈을 실행했다.

최초 사본 실험은 새 fixture가 `GFrameCounter+1`을 매 호출 뒤 원래 값으로 되돌려 같은 프레임 번호를 반복했다. 실제 접지/이동이 진행되지 않은 **검사 준비 실패**이며 Baseline/Fixed 모두 게임 재현/수정 성공의 근거로 사용하지 않는다. 실패 보고서와 원인을 보존하고 자기 대형 사본을 폐기했다. 새 fixture는 단조 증가하는 별도 프레임 번호를 사용하도록 보완했다.

그사이 HP/MP 담당이 **00:18 KST 전체 Source/Config/Plugins·공유 UHT/UBT/Intermediate/OBJ·LIB·RES/rsp·정상 출력 읽기 예약을 명시적 해제**했다. 본인의 원본 Source4/공유 빌드 범위를 새로 기록/재조회하고, 타 담당의 Source3/러너 쓰기와 독립 사본 실행·원본 HUD 읽기는 유지한 채 순차 진행했다. 타 담당 예약/프로세스를 임의로 해제/종료하지 않았다.

### 42.2 기존 정상 코드의 실패 재현

테스트만 원본에 추가한 `BaselineActual` 정상 빌드는 CPP1/링크2·종료0·컴파일/링크 경고0이다. 실제 CMC 접지와 엔진 `UInterpToMovementComponent`의 수평600cm/s 운반 뒤 승인 넉백을 실행했다. 컨트롤러 없는 캐릭터의 NPC 공용 경로와 실제 PlayerController/PlayerState/운영 보안 컴포넌트를 붙인 플레이어 경로를 각각 60/15Hz에서 확인했다. 플레이어는 기본 생성 유예 뒤 실행하며 제어 중 강제 횡방향 입력도 시도한다.

기존 코드에서 60Hz 두 경로는 Completed/상대291.667cm였지만 **15Hz 두 경로는 상대33.333cm만 밀리고 ForeignMovement(6)로 종료**됐다. 정상 운반40cm가 불연속25cm를 넘었기 때문이다. 별도 between-moves 검사는 엔진의 based movement/base 저장을 이동 갱신 사이 두 번 호출해 실제60cm를 운반했는데, 역시 활성 넉백을 유지하지 못했다. 정상 접지·충분한 운반 검사는 통과했고 두 새 사례만 Fail/UE 종료0인 유효한 수정 전 결과다.

### 42.3 공용 운반 기준 공유

38절에서 에어본에 적용한 실제 서버 운반 누적량을 수평 넉백에도 사용한다. 각 실행 시작에 현재 누적량을 고정하고, 이동 갱신에서 `OldLocation - (현재 누적량 - 직전 누적량)`과 직전 넉백 위치의 2D 차이를 비교한다. 현재 이동 내부와 원격 패킷 사이 운반을 모두 제거하며 캐릭터 자체의 외부 이동은 남는다. 매 정상 갱신 후 위치/운반 기준을 함께 저장한다.

기존 private `AirborneLastBaseMovementTotal`을 **`DisplacementLastBaseMovementTotal`로 이름만 바꾸어 재사용**했다. 에어본/수평 넉백은 기존 승인 경로에서 서로 배타적이며 두 실행 모두 시작 시 기준을 새로 설정한다. 중복 FVector나 별도 플레이어/NPC 상태 관리자를 만들지 않았고 클래스 필드 수/순서·복제/RPC/태그는 그대로다. 에어본 쪽 알고리즘은 변수 이름만 바뀐다.

**25cm 불연속 상한·프레임 내부 거리 상한·벽/사망/정화·이동 승인/보안 정책과 기존 서버 발판 신뢰 조건은 유지**한다. 추가 거리 허용이나 발판 탑승 면제를 넣지 않았다. 원본 변경은 다음 4개뿐이다.

- `Source/Project_RPG/Public/Component/Combat/RPGCombatControlComponent.h`
- `Source/Project_RPG/Private/Component/Combat/RPGCombatAirborne.cpp`
- `Source/Project_RPG/Private/Component/Combat/RPGCombatKnockback.cpp`
- `Source/Project_RPG/Private/Tests/RPGKnockbackTests.cpp`

### 42.4 최종 정상 모듈과 집중 native 26개

`FixedActual`의 정상 Editor 빌드는 **CPP42/링크2·종료0**이며 기존 `RPGGameplayAbility.cpp` NonInstanced 폐기 예정 경고1이 남았다. 다음 정상 설치 DLL과 같은 해시의 실제 로드 사본에서 새2+기존 넉백6+에어본18을 한 번에 실행했다.

```text
201E7DAA7EAF65BFEF95DC7BC6305207BE5EF84BBC2D5CAC9DA49C13B7948F18
```

**26/26 Success·UE 종료0**이다. 25개는 무경고, 기존 `Airborne.FutureClockAndOwnedPoseResync`에는 Guardian 등장 몽타주의 `JumpToSectionName Cilmb ... AM_Guardian_Spawn` 경고1이 남았다. 새2는 무경고 성공이며 전체26 무경고라고 표현하지 않는다. 기동 Error15/검사 발견 이후 Error0과 fatal/assert/ensure0도 구분했다.

| 실제 네이티브 운반/넉백 | NPC 공용 경로 | 플레이어 경로 |
|---|---:|---:|
| 60Hz 종료/상대 이동 | Completed / 291.667cm | Completed / 291.667cm |
| 15Hz 종료/상대 이동 | Completed / 266.667cm | Completed / 266.667cm |

제어 중 횡방향 입력 차단과 플레이어 보안 위반0·자기 이동 승인 해제를 확인했다. 새 `KnockbackPlatform.BetweenMovesAndForeignTeleport`는 **정상 운반60cm 뒤에도 넉백 유지 → 추가 무단35cm 이동은 ForeignMovement 종료/승인 해제**를 확인한다. 기존 승인/면역·중복/교체·스턴/중력·사망/회수·보안 창과 에어본 경계를 최종26에 포함했다.

명령 거리300cm의 정확한 재현까지 보증하지 않는다. 60Hz291.667/15Hz266.667의 관측은 남아 있으며 기간 만료/물리의 이산 시간 오차는 이번 운반 오인 수정의 대상이 아니다. 충돌 없는 평지에서도 정확한 최종 거리 보장이 필요하면 별도 후속으로 다룬다.

### 42.5 증거·정리·미검증 범위

최종 결과는 `Saved/Tests/KnockbackPlatform-20261101/Final.proof.json`의 `passed=true`다. 유효한 수정 전은 `BaselineActual/`, 정상 수정 후26개는 `FixedActual/`의 JSON/로그/명령/입력 해시에 있다. 최종 선택 입력 **1,440개**가 빌드→검증→집계 동안 불변이고 최초 사본의 원본 소스 집합과 비교해 위4개 외 변경0을 확인했다. 초기 사본 fixture 실패의 `Verification.proof.json`은 성공으로 덮어쓰지 않았다.

실패/성공의 자기 입력/생성 헤더/OBJ·LIB/RES 사본·분리 DLL/PDB·RuntimeProject/UserDir·캐시·중복 콘솔 로그를 폐기했다. 누적 **2,990,787,074 bytes(2.7854GiB)** 정리·임시 잔여/대형 예외0이며 원본 정상 Binaries/Intermediate·유지할 소스/검사/작은 결과는 보존했다. 폐기한 분리 모듈·입력 사본의 해시는 당시 증거이며 동일 사본 재실행은 불가능하다. 본인 Source4·공유 빌드/검증 입력 예약을 해제한다.

이번 검증은 승인 적중을 사용하는 로컬 권한 native 월드의 실제 CMC/GAS/kinematic 발판이다. 실제 `BP_Player` 원격 넉백/AI NPC·Iris 지연/손실·GPU/물리 입력·회전/물리 발판은 새로 실행하지 않았다. 41절의 이전 F1 실제 에어본 네트워크 통과를 이번201E 모듈/수평 넉백의 네트워크 결과로 확대하지 않는다.

## 43. 네트워크 최종 상태의 누락값 성공 판정 수정 — 2026-11-01

일반 디버프 후속 착수 때 `menu-frontend-20261101`이 전체 UE Source와 공유 빌드 입력을 예약하고 있어, AGENTS.md의 독립 작업 전환 절차로 기존 전투 네트워크 러너의 실제 결함을 수정했다. 일반 디버프 C++/에셋은 이번에 추가하지 않았다. 보스의 기존 경직·스턴·변위·둔화 면역과 화상 허용 정책도 그대로다. 다음 구현은 최신 조율 표/실제 소비 경로를 다시 확인해 별도 예약한다.

### 43.1 재현과 수정

`Run-GuardianMelee2NetworkTest.ps1`의 수정 전 실제 결과 판정 블록을 추출해 41절 F1의 작은 결과 사본에 적용했다. 정상 사본 외에도 **세 역할의 최종 좌표 누락, HP 누락, 발판 드리프트 누락, 문자열 `iris="false"`, syntheticDamage/replacementGrant 누락**의 다섯 잘못된 결과가 모두 통과했다. PowerShell의 null 산술/비교와 문자열 truthiness가 원인이다. 원래 블록과 입력 해시·재현은 `Saved/Tests/GuardianResultContract-20261101/Baseline.proof.json`에 보존했다.

새 `Scripts/Network/GuardianMelee2ResultContract.ps1`을 실제 실행 경로와 `-AuditResultRun <결과 디렉터리> [-MovingPlatform]`가 함께 사용한다. 역할별 JSON object/정확한 역할·BP 클래스, 실제 boolean, 유한한 JSON 숫자를 먼저 확인한다. 누락/null·숫자 문자열·배열·비유한 값·잘못된 플랫폼 모드를 거절하고, 기존 C++의 살아 있는 대상 피해/상승50cm/착지 높이8cm·보안 위반0/이동 승인 해제도 대조한다. 위치 오차30cm, 발판 이동500cm 이상/드리프트50cm 이하와 Down/GetUp 관측을 유지한다. 발판에서는 서로 다른 완료 시각의 월드 X/Y 대신 상대 X/Y를 비교한다.

읽은 결과 바이트 자체의 SHA256과 판정은 `ResultContract.proof.json`에 기록하며 실패 재감사도 `passed=false`/원인을 써 이전 감사의 PASS를 교체한다. 이 파일은 결과 내용의 감사이며 UE 실행/모듈 로드/로그 검사까지 증명하는 `Network.proof.json`을 새로 만들거나 갱신하지 않는다. 기존 `-AuditRun`의 상승 오차35cm 감사는 유지한다.

### 43.2 확인과 범위

`Scripts/Tests/Test-GuardianMelee2ResultContract.ps1`의 최종 **127개**(합성126 + 41절 실제 3역할 결과 사본1)와 직접 영향을 받는 기존 상승 판정 **25개**가 통과했다. 누락/형식/역할·피해·보안·발판/착지 조건, 경계값 통과/초과 거절, 과거 PASS의 실패 교체를 확인했다. 현재 형식이 없는 더 오래된 보고서에 필드를 임의 보충해 성공으로 처리하지 않았다. 스크립트3 구문/공백 검사와 검사 입력3/실제 결과 원본3의 해시 확인을 완료했다.

최종 증거는 `Saved/Tests/GuardianResultContract-20261101/Final.proof.json`이다. 실제 결과 감사는 `52bf8f17d5fd40c9a42d9351e72c7ba3/Verification.proof.json`, 연관 상승 감사는 `Saved/Tests/GuardianMotionContract-20261031/20880fed365341fc91d6865a1796e5a9/Verification.proof.json`이다. 초기 합성126 통과는 별도 기록이며 최종127에 중복 합산하지 않았다. 각 실행 finally로 자기 fixture/결과 사본 **645,732 bytes(630.60KiB)**를 폐기했고 잔여/예외0, 작은 명령/해시/결과만 보존했다. 본인 PS3/임시 경로/문서 예약을 해제한다.

게임 Source/에셋/정상 DLL/UBT/UE 실행 변경0이다. 따라서 일반 디버프 추가나 새 게임 네트워크 실행 완료를 의미하지 않는다. 종료 신호 동시 처리로 인한 종료 구간 연결 오류, 수평 넉백의 실제 원격/발판 검증 등 기존 후속은 별도다.

## 44. 공용 방어력 감소 — 2026-11-01

### 44.1 범위와 데이터 계약

`FRPGDefenseReductionSettings`와 `URPGDefenseReductionEffect`가 기존 `URPGAttributeSet::Defense`를 사용한다. Player 부여 데이터의 `DefenseReduction`, NPC 시작 데이터의 `AttackDefenseReductions`(가장 가까운 공격 부모 설정 우선), `SkillDefinition.DefenseReduction`을 같은 승인 적중 경로로 연결했다. 기본 Percent0은 비활성이며 1~80%, 기간0.05~30초를 허용한다. 능력 부여/스킬 실행 때 값을 복사하며 Reset은 다음 실행에 이전 설정을 남기지 않는다. 몬스터별 새 GA BP를 만들지 않는다.

첫 승인 피해는 적용 전 방어력으로 계산하고, 살아 있는 대상에게 후속 디버프를 부여해 **다음 공격부터** 감소한 방어력을 소비한다. 기존 근접/스킬 승인·거리·팀·피해 guard를 유지한다. 공용 native 수신 함수에 새 BP/RPC 적중 승인 진입점을 추가하지 않았다. 현재 운영 공격/스킬 에셋에는 임의 수치를 배치하지 않는다.

### 44.2 중첩·면역·수명

기간 GE가 게임 상태/만료의 원본이다. `MultiplyCompound`로 각각 남은 방어력을 곱한다. 예를 들어 방어력100에 20%와30% 감소는56이며, 각 효과는 자기 기간/핸들을 갖는다. 최대8개이고 합산으로 방어력을0/음수로 만드는 연산을 사용하지 않는다. 장비의 기본 방어력이나 다른 버프가 바뀌면 GAS가 현재 값을 재평가하며 종료 때 과거 방어력을 직접 복원하지 않는다.

경직·스턴·변위·둔화 면역과 독립이다. 보스의 기존 기본 면역을 유지하면서 방어력 감소는 기본 허용한다. `bDefenseReductionImmune`, 개별 `Shared.Immunity.Status.DefenseReduction` 태그 또는 기존 StatusEffects 그룹으로 새 부여를 차단할 수 있다. StatusEffects 그룹 비트4의 적용 종류에 이 디버프를 추가했고 기존 면역 비트/enum 값은 보존했다. 개별 방어력 감소를 선택하는 새 스킬 면역 비트는 추가하지 않았다. 면역은 이미 부여된 효과의 정화를 뜻하지 않는다.

공용 ControlComponent는 자기 GE/핸들과 승인 receipt를 관리한다. 동일 receipt 재전달과 면역 해제 뒤 재전달은 차단한다(기존 공용 방식과 같은 최근64개 한도). 정화는 자기 효과/적용 중인 자기 효과만 제거하고 외부 장비·버프/다른 소유 효과를 보존한다. 적용 콜백 중 정화/사망/재사용의 세대 변경을 다시 검사한다. **부여가 완료된 능력치 디버프는 공격자 파괴 후에도 남고**, 대상 사망/Avatar·ASC 재사용/EndPlay에는 제거된다. 실제 레이드 보스의 전투 번호/전투 중 상태를 확인해 유휴 상태 부여를 거절하며 리셋·전멸 뒤에는 기존 컴포넌트 갱신에서 정리한다.

이번 단계는 기존 Defense 속성/상태 태그의 복제를 사용하며, 새 상태 아이콘·남은 시간 위젯/전용 표현 스냅샷은 추가하지 않는다. 실제 다인 지연/손실·늦은 참가·운영 스킬의 수치/중첩 밸런스는 별도 검증 범위다.

### 44.3 검증·정상 설치 완료

원본 소스28경로와 공유 NoLink 빌드/입력 링크 자원을 예약하고 사설 검증을 진행했다. 이후 menu-frontend의 완료/전체 해제 및 원본 DLL 매핑0을 확인해 정상 출력을 추가 예약하고 순차 설치했다. 정상 빌드는 **링크6/metadata1·종료0·추가 CPP0**이며 다음 모듈을 설치했다.

```text
3761E2A027042DDFAA6236D9FFCEC30822C709E2C3A71FA64527DF8DBC64E71E
```

- 사설 모듈 `0D42E4A5…F98D82AB` 실제 로드에서 **31/31 무경고 Success·UE 종료0**. 새6와 기존 면역/둔화/화상/경직25를 포함한다. 최초 집계 때 관련 검사 하나를 빠뜨려30개를 예상한 러너는 false를 기록했지만 실제31개·발견 목록/정확한 이름·종료0을 재감사했다. 원래 false 결과를 덮어쓰지 않고 `PrivateVerified/Audit.proof.json`으로 바로잡았으며 이 때문에 UE 검사를 반복하지 않았다.
- 같은 소스28경로로 정상 설치한 모듈의 실제 로드 사본에서 **새6/6 무경고 Success·UE 종료0**을 확인했다. 설치/사본/관측 SHA가 같고 사설 검사→정상 빌드→정상 검사→최종 집계의 소스28 해시도 모두 같다. 두 최종 실행은 각각 기동 Error15/검사 발견 이후 Error0이다.
- 실제 공용 승인 피해 진입점에서 HP100→95(방어력2) 뒤 디버프/방어력1, 다음 타격 HP85를 확인했다. 기간 GE의20%×30% 중첩/만료, 현재 base/외부 버프 보존, 면역·receipt·상한8, 적용 중 정화/대상 사망·재사용/출처 파괴 뒤 유지, 실제 Boss 계층의 기본 경직·스턴 면역과 방어력 감소 허용·Idle 거절/전투 리셋 정리를 확인했다.

최초 .NET 실행 환경 실패, 직접 include/검사용 보호 함수 접근의 컴파일 실패, `-NoLink`가 남긴 옛 링크 응답의 신규 OBJ 누락은 초기 기록으로 보존했다. 사설 링크에 본인 새4개 번역 단위만 명시적으로 추가해 해결했고 정상 UBT는 표준 입력으로 설치했다. 최초 보스 검사는 비동기 시작 자료 준비 전에 검사해 실패했으며, 준비 완료를 기다리는 latent fixture로 보완했다. 첫 실제 결과의 나머지30 통과와 후속31 무경고 통과를 구분한다.

최종 증거는 `Saved/Tests/DefenseReduction-20261101/Final.proof.json`의 `passed=true`, `normalIntegrationComplete=true`다. 사설 결과/감사는 `PrivateVerified/`, 정상 설치·같은 모듈의 검사는 `Installed/`에 있다. 실패/성공의 사설 DLL/PDB·실행 사본·HUD overlay·UserDir/캐시 등 **1,034,753,944 bytes(약986.82MiB)**를 자동 폐기했고 임시 잔여/정리 예외/자기 UE 프로세스0이다. 작은 명령/소스·링크 입력·모듈 해시/필요 로그와 보고서만 보존하며 정상 Binaries/Intermediate는 유지한다. 폐기 사설 모듈/실행 사본의 해시는 당시 증거다. 본인 소스28/공유 빌드·정상 출력/검증 입력 예약을 해제한다.

검사는 공용 native 캐릭터/실제 Boss 계층의 서버 권한 GAS와 실제 피해 승인 경로다. 실제 BP_Player 다인 네트워크/늦은 참가·UI 아이콘·물리 입력·Shipping 및 운영 공격별 수치 배치는 새로 검증하지 않았다. 일반 디버프 중 방어력 감소의 공용 기능이 추가된 것이며 중독/출혈·받는 피해 증가까지 구현한 것으로 확대하지 않는다.

## 45. 공용 받는 피해 증가 — 2026-11-01

### 45.1 적용 범위와 계산 계약

`FRPGDamageTakenIncreaseSettings`의 Percent/Duration을 Player 부여, NPC `AttackDamageTakenIncreases`의 가장 가까운 공격 부모 설정, SkillDefinition/실행 스냅샷에 연결한다. Percent0은 비활성, 유효한 활성 범위는1~100%·0.05~30초다. 같은 GA를 재사용하며 운영 공격/스킬 에셋의 기본0을 임의로 변경하지 않는다.

승인된 첫 피해 뒤 수신자의 `URPGDamageTakenIncreaseEffect`(기간/상태 태그/SetByCaller 퍼센트)를 부여하고 **다음 일반 공격·스킬·공용 ExecCalc를 사용하는 패턴부터** 피해를 늘린다. 별도 Attribute를 추가하지 않는다. 서버의 `GetDamageTakenMultiplier()`는 자기 수신 컴포넌트가 승인/소유한 활성 GE만 조회하고, 억제·다른 소유 효과·오래된 대상 생명/ASC/보스 전투를 제외한다. 배율은 계산 결과이며 별도의 게임 상태나 클라이언트 권한 원본이 아니다. 퍼센트에서 double로 계산해 float1.3 배율의 정수 변환 시10피해가12로 잘리는 경로를 피하고, 기존 최종 정수 변환/상한 규칙은 유지한다.

`UGE_ExecCalc_TakeDamage`는 기존 방어력/치명타 계산 뒤 이 배율을 곱하고 **기존 FRPGDamageExecutionGuard의 최종 상한 확인 전에** 전달한다. 상한 초과는 기존 규칙대로 승인 거절이며 상한을 키우거나 증폭 뒤 HP를 직접 고치지 않는다. 수치 저작 때 기존 공격/스킬의 최종 피해 예산도 함께 고려한다. 방어력 감소와 독립적으로 조합할 수 있다. 예: BaseDamage10/방어력2의 첫 피해5 뒤 방어력50% 감소와 받는 피해50% 증가가 생기면 다음 계산은10/1×1.5=15다.

화상은 승인 시 확정한 고정 최종 회차 피해의 기존 계약을 유지하며 이 ExecCalc 경로를 거치지 않는다. 이번 기능이 화상 회차 피해까지 바꾼 것으로 해석하지 않는다. Imported `DamageReductionPercent`의 의미/소비를 임의로 바꾸지 않는다.

### 45.2 중첩·면역·수명

각 GE는 자기 핸들/기간을 가지며 최대8개가 독립 곱으로 중첩된다. 20%와30%는1.2×1.3=1.56배, 100%8개는 최대256배다. 유효하지 않은 배율은 계산에서 제외한다. 다른 방어력/장비/버프를 종료 시 되돌리지 않으며 GAS의 활성/억제/제거 상태를 그대로 소비한다.

보스의 기존 경직·스턴·변위·둔화 면역과 독립이고 기본 허용이다. 대상 기본 `bDamageTakenIncreaseImmune`, 개별 `Shared.Immunity.Status.DamageTakenIncrease`, 기존 StatusEffects 그룹으로 새 부여를 막을 수 있다. 그룹의 기존 비트4를 새 종류에도 적용하고 기존 enum 값/면역 비트는 보존한다. 이미 걸린 효과의 정화와 새 부여 면역은 별개다.

최근64개 receipt 중복 방지, 최대8개 소유 효과, 적용 중 정화/수명 변경 재검사, 자기 효과만 제거하는 정화, 대상 사망/Avatar·ASC 재사용/EndPlay 정리를 사용한다. 부여가 끝난 효과는 공격자 파괴 후에도 남는다. 실제 보스의 Idle 부여는 거절하며 전투 번호 변경/리셋·전멸은 소비 시 즉시 제외하고 기존 컴포넌트 갱신에서 자기 GE를 제거한다. 새 BP/RPC 적중 승인 API나 몬스터별 GA BP를 만들지 않는다.

### 45.3 정상 반영·실제 모듈 검사 완료

본인 소스28경로/공유 빌드·정상 출력/자기 사본을 예약했다. Normal1은 float1.3 배율의 정수 변환에서10→12가 되는 수치 경로를 발견해 **자기 빌드/자식 컴파일러만** 중단하고, 퍼센트 보관/double 계산과10→13 실제 피해 검사를 보완했다(`Precision.proof.json`, Normal1의 취소/실패 기록 보존). Normal2는 본인 소스 밖의 로비 `CanUseCharacterConfiguration(false)` 호출과 `(bool bEdit,bool bRetry)` 선언 불일치 C2660으로 실패했다. 해당 Source8 담당이 `(false,false)`로 수정하고 고정 SHA8/빌드 포함을 인계했으며 본인은 로비 원본을 편집하지 않았다. 본인28+인계 읽기8을 검사해 Normal3을 순차 진행했다.

최종 정상 Editor 빌드는 **CPP78/링크4/metadata1·종료0·C++ 경고0**이며 다음 모듈을 설치했다. 앞선 컴파일 캐시를 재사용했고 초기 중단/실패를 성공으로 덮어쓰지 않았다.

```text
46083497DABC9E0E239C0532B401DBB02ED3DF07E352D304515ED5F702DEADA9
```

같은 해시의 실제 로드 사본에서 **38/38 무경고 Success·UE 종료0**이다. 정확한 목록은 새7개와 기존 방어력 감소/면역/둔화/화상/경직31개이며 누락/중복 없이 확인했다. 기동 Error15/검사 발견 이후 Error0·fatal/assert/ensure/Blueprint 실행 오류0을 구분한다. 본인 소스28과 인계된 읽기 소스8은 빌드→검사→최종 집계에서 해시가 같다.

- Player/NPC grant와 SkillDefinition/실행 스냅샷·reset, 중첩/억제·복구·만료/외부 방어 버프 보존, 면역/receipt/효과8개 상한, 적용 중 정화/사망·재사용/출처 파괴 뒤 유지가 통과했다.
- 실제 승인 피해 경로에서 HP100→95 뒤 방어력50% 감소와 받는 피해50% 증가를 함께 부여해 방어력1/배율1.5, 다음 타격 HP80을 확인했다. 별도 최종 cap15에서는 배율2의 계산 피해20을 거절해 HP를 보존했고, 억제 뒤 기본10 피해 및30% 증가의13 피해도 확인했다.
- 실제 `ARPGBossCharacter` 계층의 시작 자료 준비/전투 진입을 기다려 기본 경직·스턴 면역을 확인하고, 디버프 부여/Idle 거절·리셋 제거를 확인했다. 새 상태가 행동 제어를 추가하지 않는 것도 검사했다.

최종 증거는 `Saved/Tests/DamageTakenIncrease-20261101/Final.proof.json`의 `passed=true`, `normalIntegrationComplete=true`다. `Normal3/`에 실제 명령/모듈 로드·SourceInputs28/LobbyHandoffInputs8·결과/필요 로그가 있고 초기 Normal1/2 실패는 별도다. NPC 관측 CPP1은 읽기 관측 전후 SHA가 같으며 본인 쓰기0, Normal3의 해당 CPP 재컴파일0(기존 OBJ 재사용)으로 기록했고 새 NPC 실전 검증으로 확대하지 않는다.

정상/중단/실패의 자기 Temp를 자동 정리했고 **156,444,867 bytes(149.20MiB)**의 복사 DLL/실행 사본/HUD overlay/UserDir·캐시를 폐기했다. 임시 잔여/정리 예외/자기 UE 프로세스0이며 정상 Binaries/Intermediate·작은 명령/해시/필요 로그/보고서만 유지한다. 폐기한 실행 사본/실제 로드 경로는 당시 증거다. 본인 Source28/공유 UHT·UBT·Intermediate/정상 출력·검증 입력과 인계 Source8 읽기 예약을 해제하며 다른 담당의 쓰기 예약은 유지한다.

이번 검사는 서버 native/GAS·실제 피해 승인 경로/실제 보스 계층이다. 실제 BP_Player 다인 지연/손실·늦은 참가·상태 UI·물리 입력·Shipping·운영 공격별 수치 저작은 별도다. 기본0인 운영 데이터를 바꾸지 않았으므로 사용할 공격/스킬에 수치를 지정해야 한다. 고정 최종 피해 화상은 이전 계약을 유지하며 중독·출혈까지 추가한 것으로 표현하지 않는다.

## 46. 방어력 감소·받는 피해 증가의 실제 BP 네트워크 확인 — 2026-11-01

### 46.1 실행 범위와 방법

§44~45에서 남긴 실제 BP 다인 검증을 완료했다. `BP_Player` 두 명과 원본 `BP_RaidSwordMan`을 Entry 맵의 전용 서버1/클라이언트2에 생성하고, 세 프로세스 모두 실제 Iris와 지연80~160ms/손실5%를 검사했다. 관찰자는 첫 플레이어에게 두 디버프가 적용된 뒤 접속한다. 소유자/관찰자가 실제 복제 HP·Defense·상태 태그·보스 전투 상태를 확인해 단계별 ack를 남긴 뒤에만 서버가 다음 단계로 진행한다. 파일 제어는 관측 순서에만 사용하며 클라이언트의 속성/태그를 검사 값으로 덮어쓰지 않는다.

새 파일은 `Scripts/Network/Fixtures/CombatDebuffNetworkProbe.cpp`와 `Scripts/Network/Run-CombatDebuffNetworkTest.ps1`이다. 다른 담당의 공용 헤더/Source·공유 빌드 작업을 피하려고 기존 정상 타깃의 OBJ/LIB/응답 파일·헤더/생성 헤더를 짧게 복사·전후 해시 검사하고, 새 CPP만 PCH 없이 사설 컴파일/링크했다. 게임 Source/UHT/UBT·정상 Binaries·기존 에셋 쓰기는 없다. PlayerHUD와 작성 중인 `GA_Player_DrawHUD`는 자기 runtime의 실제 파일 사본으로 닫고, 그 밖의 안정된 Content는 읽기 junction을 사용했다. 닫힌 모듈을 만든 뒤 원본 컴파일 입력 예약을 해제해 다른 담당의 정상 빌드를 막지 않는다.

피해 자극은 기존 native 검사 GA를 메모리에서 직접 부여한 뒤 실제 `BP_ApplyEffectSpecHandleToTarget` 승인 경로를 호출한다. 검사 수치는 HP10000/Attack1/Defense2/CritChance0이며 보스 AI를 멈춘다. **실제 BP와 실제 서버 피해/상태 부여·GAS 복제의 검사이며, 운영 GA 원본/물리 입력·무기 트레이스/자연 몽타주 적중 검사는 아니다.** 운영 공격별 데이터나 원본 BP 기본 수치는 바꾸지 않았다.

### 46.2 확인 결과

세 역할 모두 **14단계·마스크49150**, 합계 **42개 관측**, 서버 승인 피해9회가 통과했다. 각 JSON의 bool/number 자료형·클래스/역할·단계 순서·고정 HP/Defense·면역/상태·피해량을 검사하며 누락값을0이나false로 변환해 통과시키지 않는다.

| 확인 항목 | 실제 결과 |
|---|---|
| 적용 중 늦은 참가 | 플레이어 HP9995/Defense1·두 상태 태그를 소유자와 새 관찰자가 모두 확인 |
| 보스 기본 면역과 일반 디버프 | 기존 경직·기절 기본 면역=true를 유지하면서 Defense 감소/받는 피해 증가 적용, 행동 차단=false |
| 조합과 중첩 | 보스 첫 피해5 뒤 Defense1/서버1.5배, 다음15피해 뒤 Defense0.8/1.95배, 다음24피해로 HP9995→9980→9956 |
| 개별 기간 | 짧은 중첩 만료 뒤 Defense1/1.5배로 복귀; 이후 단독 효과도 자연 만료해 Defense2/1배 복귀 |
| 정화 | 플레이어/보스 두 종류를 정화해 속성과 태그가 두 클라이언트에서 복구 |
| 새 부여 면역 | 플레이어의 두 종류 기본 면역·보스 StatusEffects 그룹 면역 중 피해5는 적용, 새 디버프는 거절 |
| 보스 리셋 | 양쪽이 활성 상태를 확인한 뒤 Resetting 진입, 두 GE/태그 제거·Defense2/1배 복구; HP9941 보존 |

이번 실제 플레이어 ASC는 **Full(enum2)**, 보스 ASC는 기존 **Minimal(enum0)** 이었다. 플레이어는 두 클라이언트에 GE가 관측됐고, 보스는 클라이언트 활성 GE가0이어도 Defense·상태 태그·HP가 일치했다. 받는 피해 계산 배율은 서버에서만1.5/1.95이며 클라이언트 조회는 계약대로1이다. 복제 표시를 서버 피해 권한으로 사용하지 않는다.

관찰자→소유자→서버 순서로 종료해 각 exit0을 확인했다. 실제 세 프로세스의 모듈 경로/해시는 동일하다. 고정 입력을 해제한 뒤 다른 담당이 정상 빌드를 진행할 수 있으므로 아래 해시는 **이번 실행 당시의 증거**이며 이후 전체 최신 소스/모듈의 성공을 뜻하지 않는다.

```text
원본 정상 모듈: 552DB6D7777CF40D96B032F69F89C324C833A99627E656790D43F8300C421F19
실제 사설 모듈: D33AB52CE2F59549F70A17B9E36E9E53D1C002E11E353406D7B103B44D192A0C
```

### 46.3 실패·로그·정리와 남은 범위

초기2회는 공유 정의 헤더 누락, 공용 서버 타입 헤더를 통한 작성 중 Http 헤더/기존 generated 줄 위치 불일치와 방어력 GE 헤더 사본 누락으로 컴파일 단계에서 실패했다. 자기 사본 입력을 보완하고 실제 Controller/PlayerState의 native 클래스를 로드해 불필요한 공용 헤더 포함을 피했다. 타 담당 원본을 고치지 않았고 두 실패를 성공으로 덮어쓰지 않았다. 세 번째는 컴파일/사설 링크0, 위 실제 네트워크/자료형 검증/세 종료0이다. 성공 뒤에는 finally에서 프로세스 사용 해제 실패도 `passed=false`로 전달하도록 한 줄 보완하고 PS AST만 확인했으며 네트워크를 반복하지 않았다.

로그의 `: Error:`·fatal/assert/ensure/Blueprint 실행 오류는 세 역할 모두0이다. **로그 전체 무경고 검사는 아니다.** 기존 콘텐츠의 사망 음성 의존 패키지 누락 `LoadErrors`가 각1개, 엔진 모듈 추가 로드 Iris 경고가 서버2/소유자1/관찰자1개이며 전체 Warning은48/37/37개다. 기존 unloadable BT 에셋/이동 및 기동 경고도 남겼다. 이번 일반 디버프 결과가 이 콘텐츠 결함의 수정이나 Shipping/Steam·사망/상태 UI·운영 공격 수치 저작 성공을 뜻하지 않는다.

성공/실패3회 모두 자기 `Saved/CDB/<12hex>`의 입력·OBJ/LIB·DLL/PDB·runtime/HUD 사본/UserDir·캐시를 자동 폐기했다. 합계 **8,074,946,931 bytes(7.520GiB)**, 임시 잔여/정리 예외/자기 UE 프로세스0이다. 실제 실행은 `DDC=(EnginePak,Local)`로 기존 엔진 Local DDC를 사용했으며, 원본 공유 DDC/정상 Binaries/Intermediate는 삭제 대상에서 제외했다. 작은 명령/소스·모듈 해시/관측 JSON/필요 로그만 보존한다. 폐기한 사설 모듈은 재실행할 수 있는 보존본이 아니다.

최종 집계는 `Saved/Tests/CombatDebuffNetwork-20261101/Final.proof.json`, 성공 실행은 `34ce18e3f1a84f849f22635638dcfb16/Verification.proof.json`이다. 이전 native 완료 소스28개는 이번 검사 전후 동일하며 원본 정상 DLL도 종료 후 감사 시 위 해시였다. 본인 검사 CPP1/러너1·문서/읽기·사본/포트·실행 자원 예약을 모두 해제한다. 다른 담당의 Source/공유 빌드/에셋 예약은 유지한다.

## 47. 머리 위 상태 라벨을 이름만 표시 — 2026-11-02

사용자 요청에 따라 플레이어/NPC 공용 머리 위 라벨을 아래처럼 변경했다. `둔화 40%`가 현재 속도의40% 감소인지 기준 속도의40%p 차감인지 혼동하게 하는 표기를 없애고, 화상 같은 지속 피해 디버프는 이름만 표시한다. 향후 추가할 DOT도 머리 위에서는 같은 이름 표시 원칙을 따른다.

| 기존 표시 예 | 변경 표시 |
|---|---|
| 둔화 40%  2.8초 | 둔화 |
| 화상 x2  3.5초 | 화상 |

`Private/UI/Combat/RPGSlowStatusWidget.cpp`와 `RPGBurnStatusWidget.cpp`의 표시 함수 두 곳만 수정했다. 숫자/시간 포맷과 불필요한 시계 include를 제거하고 `NSLOCTEXT`의 이름 문자열을 사용한다. 기존 컴포넌트가 활성/만료/정화/사망 및 표시 옵션에 따라 위젯을 생성·제거하는 경로는 유지한다. 둔화의 실제 이동 배율이나 화상 피해/중첩/서버 수명·복제는 바꾸지 않았고, 미구현 상태 종류/이름 UI까지 추가한 작업은 아니다.

본인 CPP2 쓰기와 정상 빌드를 예약했지만 편집 뒤 재조회에서 HUD 담당의 `RPGHUD.h/cpp` 및 신규 수명 검사2개 쓰기 예약이 추가된 것을 확인했다. 전체 Source 고정 읽기와 공유 UHT/UBT/정상 출력 예약을 즉시 해제하고 정상 빌드를 시작하지 않았다. 독립 작업으로 두 위젯의 기존 compile response/전투 헤더·생성 헤더/PCH만 좁게 예약했으며 그 의존 목록에는 HUD Source4가 없다. 같은 정상 MSVC 옵션으로 두 CPP를 자기 OBJ 경로에 개별 컴파일해 **각 exit0·경고0**, 고정 입력43개의 전후 SHA 변경0을 확인했다. 새 테스트/UE 실행은 추가하지 않았다.

초기 실행은 컴파일 뒤 정리 단계에서 PowerShell5.1의 BOM 없는 한글 정규식 디코딩 오류로 중단됐다. 자기 컴파일러 사용 해제/예약을 확인해 남은 OBJ를 정리하고, 실행기의 담당 조회를 ASCII 패턴으로 바꿨다. 보완 실행은 위 컴파일·해시·finally 정리를 모두 완료했다. 원래 실패는 `InitialRunnerFailure.json`에 별도 보존한다. 두 시도의 자기 OBJ/임시 응답은 모두 폐기했고 정확한 삭제량은 `Final.proof.json`에 기록한다. 원본 Binaries/Intermediate·공유 DDC/다른 담당 임시·프로세스는 변경하지 않았다.

**초기 완료는 소스 변경/개별 컴파일까지였으며, 후속48절 정상 빌드로 DLL 반영을 완료했다.** 당시 감사 시 정상 모듈은 기존 `235068FD1701D463F1EFB32BE8CBDFEC466EF09196FCFCE312726669ABBD6B10`으로 전후 동일하다. HUD Source4 작성 종료/인계 뒤 다른 담당의 정상 빌드에 본인 `ChangedSourceInputs.json`의 두 SHA를 포함해도 된다. 별도의 GA/에셋 수정이나 전투 네트워크 suite 반복은 필요하지 않다. 본인 소스2/좁은 읽기/PCH/컴파일 출력·문서 예약을 해제하며, 다른 담당의 HUD Source/에셋·닫힌 실행 예약은 유지한다.

근거: `Saved/BuildChecks/CombatStatusNames-20261102/{Compile.proof,Final.proof}.json`, `ChangedSourceInputs.json`·명령/응답/최소 컴파일 로그. 폐기한 OBJ 해시는 당시 증거이며 보존한 재실행 바이너리가 아니다.

후속 정상 반영: 48절의 정상 Editor 빌드 입력2009개에 위 두 소스의 같은 SHA가 포함됐다. 모듈 `983E24AD…26EF6446`으로 DLL 반영 대기를 해소했다. `Saved/BuildChecks/CombatStatusNames-20261102/NormalIntegration.proof.json`이 현재 통합 근거이며, 앞선 Final.proof의 `normalIntegrationComplete=false`는 초기 개별 컴파일 시점의 기록이다. 머리 위 표시의 별도 GPU 검사는 추가하지 않았다.

## 48. 마나 바 위의 디버프 아이콘·카운트다운 — 2026-11-02

### 48.1 사용자 요구와 표시

사용자가 배치를 **화면 오른쪽에 있는 마나 바의 바로 위**로 확정했다. `WBP_PlayerVitals`에 `PlayerDebuffs : URPGPlayerDebuffBarWidget` 하나를 추가했다. 부모 `WBP_PlayerHUD`와 기존 HP/MP13위젯의 속성·스킨·배치·바인딩은 보존한다. 마나 바와 같은 앵커/좌측 기준을 사용하고62단위 위, 너비390/높이58의 슬롯에 배치한다. 현재 슬롯은 bottom-center 앵커, X244/Y-310이며 마나 그룹은 X244/Y-248이다. UMG가 기존 DPI 변환을 적용한다.

- 아이콘36×36, 칸 너비42/오른쪽 간격6의 가로 배열이다. 아이콘 바로 아래에 남은 초를 표시한다.10초 미만은 소수1자리,3초 미만은 따뜻한 색으로 표시한다. 포인터를 올리면 상태 이름을 읽을 수 있다.
- 같은 종류는 아이콘 하나로 묶고, 활성 GE 중 가장 늦게 끝나는 시간을 사용한다. 살아 있는 종류의 상대 순서는 유지하고 새 종류는 뒤에 붙인다. 만료/정화로 마지막 효과가 없어지면 해당 칸을 제거한다. 칸 객체는 종류별로 재사용한다.
- 현재 분류는 경직·기절·넉백·피격이상·둔화·화상·방어력 감소·받는 피해 증가8종이다. 무기한 공중 상태처럼 종료 시각이 없는 효과는 `—`를 표시하고, 다운/기상처럼 실제 기간 GE가 있으면 그 시계를 사용한다. 임의 착지 시각을 만들지 않는다.
- 이 HUD는 로컬 플레이어 자신의 상태를 표시한다. 머리 위 둔화/화상은47절의 이름만 표시하는 정책을 유지한다. NPC 머리 위에 새 아이콘 HUD를 추가한 작업은 아니다.

### 48.2 책임과 구현 경로

| 경로 (`Source/Project_RPG` 아래) | 책임 |
|---|---|
| `Public/Private/UI/Debuffs/RPGDebuffPresentation.h/cpp` | ASC 활성 GE의 태그·남은 시간/억제 상태를 읽어 종류별 목록으로 투영, 이름·시간 문구·색상 |
| `Public/Private/UI/Debuffs/RPGPlayerDebuffBarWidget.h/cpp` | 소유 Controller/Pawn 따라가기, 배열/타이머 갱신·칸 재사용·이전 소스 정리 |
| `Private/UI/Debuffs/RPGDebuffIconWidget.h/cpp` | 외부 폰트 글리프/텍스처 없이 그리는8종 벡터 아이콘 |
| `Private/Tests/RPGPlayerDebuffHUDTests.cpp` | 실제 GAS 집계·시계/억제·정화/사망·UMG 배열/소스 교체 검사2개 |
| `Private/Tests/RPGPlayerDebuffHUDProbe.cpp` | opt-in Entry 격리 월드의 실제 렌더/위치·타이머·제거 관측 |

표시는0.1초 간격으로 현재 ASC를 읽는다. GE의 로컬 시계로 보정된 `GetTimeRemaining`을 사용하며 UI용 게임플레이 타이머/상태 저장소를 만들지 않는다. 억제/만료된 GE는 제외하고, 사망/파괴/소유 Pawn 교체 시 이전 상태를 비운다. 빈 배열에서도 소유자/새 효과를 확인할 수 있도록 모듈 자체를 Collapsed로 만들지 않는다. Player의 현재 Full ASC 복제를 소비하며 GE 적용/제거·피해·면역·속도·복제 모드를 변경하지 않는다.

공용 HUD 수명 클래스/전투 컴포넌트·GA·백엔드/DB는 수정하지 않았다. `Scripts/Editor/ConfigurePlayerDebuffHUD.py`는 기본 읽기 전용이고 명시 적용에서 Vitals 한 에셋만 저장한다. 원래 위젯들의 보존, 새 클래스/위치, 같은 결과에서 추가 저장0을 검사한다. 기존 `Scripts/Tests/Verify-PlayerVitalsAsset.py`도14개/새 자식 배치 계약으로 갱신하고 AST를 확인했다. 이 기존 전체 검사 스크립트 자체의 새 UE 실행은 추가하지 않았다.

### 48.3 빌드·실제 표시 확인

공유 전체 Source 고정과 겹친 동안 신규 모듈을 `Saved/BuildChecks/PlayerDebuffHUD-20261102/Implementation`에서 독립 작성했다. 기존 작업의 공유 입력 해제를 확인한 뒤 원본에 새8파일을 설치했다. Normal1은 본인 코드의 UE5.8 `TObjectPtr<APawn>` 추론과 `Slot` 멤버 shadow 오류로 실패했다. 본인 두 CPP만 수정한 Normal2는 CPP2/링크2/metadata1·종료0/새 C++ 경고0이며 입력2009개 파일집합/SHA가 같다. 정상 모듈은 다음과 같다.

```text
983E24ADFDBAEE298B8FDD2FEB0665F8B8009951D3C05E33482524E526EF6446
```

같은 실제 로드 사본에서 native2개 모두 Success/검사 오류0이다. 첫 검사에 기존 에셋 캐시 경고139개, 두 번째에0개가 있어 전체 무경고 통과로 표현하지 않는다. 새 HUD가 처음 생겼을 때의 현재 상태 조회, 중첩 최종 만료, 시간 감소, 억제된 층 제외, UI 조회가 실제 GE를 제거하지 않음, 정화/사망, 칸 순서와 소스 교체/이전 소스 무시·Pawn 파괴를 확인했다.

Vitals 사본에 새 자식을 저장·별도 Cmd 재로드/추가 저장0을 확인했다. 실제1920×1080 렌더에서 **활성4→화상 만료3→정화0→화상 재적용1→Pawn 종료0**을 확인했고, Native cached geometry로 아이콘 아래의 타이머와 마나 바 위의 위치를 검사했다. 방어력 감소의 남은 시간이 약11초→6.5초로 줄었다. 최종 Preview는 exit0, Error등급/fatal/assert/ensure/BP 실행 오류0이다. 최종 PNG를 직접 확인했다.

첫 렌더 결과는 올바른 Decimal JSON 수치를 검사기가 double만 허용해 거절했다. 두 번째는 표시/관측·종료가 성공했지만 game 모드의 기존 편집 도구 Python 초기화 Error58줄로 엄격 검사가 false였다. 각각 원본 실패를 보존하고 수치 자료형 검사와 화면 실행의 `-DisablePython`을 보완했다. 세 번째는 **같은 모듈에서 이미 통과한 native 보고서와 저장/재로드한 동일 에셋 해시를 검증해 재사용**하고 실패했던 화면 부분만 새 설정으로 실행해 전체 러너가 통과했다. 기존 성공 검사를 반복하거나 초기 false를 덮어쓰지 않았다.

이번 화면은 Entry의 격리된 로컬 Pawn/실제 ASC에 네 종류 native GE를 직접 부여한 UI fixture다. 원본 Vitals 스킨/실제 UMG·GAS 시계를 사용하지만, BP_Player의 자연 공격·새 UI의 Iris 다인 검증/운영 스킬별 디버프 부여를 새로 완료한 결과는 아니다. 이 범위는46절의 기존 게임플레이 복제 결과와 구분한다.

### 48.4 원본 반영·정리

다른 담당의 Vitals 관련 읽기/에셋 예약 해제를 확인하고, 원본 에셋 SHA와 사용자 에디터 부재를 재조회한 뒤 검증한 바이트를 `WBP_PlayerVitals.uasset`에 원자 교체했다. 설치 후 해시는 **F000A87D…630EC19**, 부모 `WBP_PlayerHUD`는 전후 같은 **038071F7…A150341**이다. 기존13개를 보존한 새14개 모듈이다. 소스8개는 검증 전후 동일하다. 후속 다른 정상 빌드가 모듈을 교체할 수 있으므로 위983 해시는 이번 실행 당시 증거이며 최신 전체 프로젝트 검사로 확대하지 않는다.

세 실행과 설치의 자기 사본/DLL/UserDir·메모리 DDC/임시 백업을 사용 해제 후 자동 폐기했다. 합계 **308,145,428 bytes(293.870MiB)**, 자기 PDH 임시/UE 프로세스·정리 예외0이다. 원본 Binaries/Intermediate/공유 DDC·타 담당 파일/실행은 정리하지 않았다. 최종 소스/에셋·작은 해시/명령/필요 로그/결과와 캡처만 유지하며 폐기한 실행 사본은 재실행 보존본이 아니다.

최종 집계는 `Saved/BuildChecks/PlayerDebuffHUD-20261102/Final.proof.json`이다. 성공 화면은 `1c1f4ec1cd39454ca647858a51edcce7/`, native는 `bd7f558352f94478818c9e4bc5738552/Native.report.json`, 저장/재로드 에셋은 `c29cb9a62ace4d4ca134fdccb6abbb65/`에서 인계한다. 본인 Source8/도구2/에셋1·전체 읽기/공유 빌드·사본/실행/문서 예약을 해제하고 다른 담당 예약은 유지한다.
