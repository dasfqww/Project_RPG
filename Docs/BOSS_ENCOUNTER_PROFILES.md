# 보스 난이도별 전투 프로필

같은 보스 Actor/외형을 유지하면서 서버 난이도에 따라 페이즈, 사용할 공격과 기믹,
패턴의 실행 순서 및 공격 범위 예고 표시를 선택한다. 난이도별 데이터는 완전한 정의이며
다단계 상속이나 필드별 암묵적 병합을 사용하지 않는다.

## 작성 순서

1. 난이도마다 `RPGBossDefinition` 데이터 에셋을 만든다. 각 정의의 `Phases`에
   HP 임계치, 명시적 전환, 무적/전투 AI 정지, 제한 시간과 기믹 완료 페이즈를 설정한다.
   기존 페이즈를 빼거나 다른 페이즈/기믹 GA로 교체하는 방법으로 난이도를 구성한다.
2. `RPGBossEncounterProfiles` 데이터 에셋의 `Profiles`에 Easy/Normal/Hard/Hell과
   각 정의를 연결한다. 미지정 난이도는 시작을 거절한다. 공통 정의를 쓸 경우에만
   `Fallback`을 명시한다. 명시된 null 항목은 fallback으로 덮어 주지 않는다.
3. 보스의 `BossAIComponent.EncounterProfiles`에 프로필 에셋을 지정한다.
   비어 있으면 기존 `Definition`을 그대로 쓴다. 기존 보스 에셋의 밸런스는 자동 변경하지 않는다.
4. 각 페이즈의 `Patterns`에 패턴 항목을 작성한다. `PatternId`는 해당 페이즈 안에서
   고유한 GameplayTag다. 같은 GA를 여러 항목이나 다른 페이즈에서 재사용할 수 있다.
   GA의 `ActionTag`는 머티리얼/색상 선택용이며 패턴 식별자가 아니다.
5. NPC startup data에 사용할 GA를 미리 지급한다. 선택기는 능력을 새로 지급하지 않는다.
   여러 난이도에서 쓰는 GA의 합집합을 지급해 두거나 기존 startup 구성을 별도로 작성한다.
   지급하지 않은 패턴은 실행할 수 없으며, 순차 실행에서는 그 항목에서 대기한다.

기존 `AbilityPatterns`/`bRandomizePatterns`는 호환용으로 유지한다. 새 `Patterns`와
같은 페이즈에서 함께 설정하면 검증 오류다. 기존 `PhaseEntryAbility`와 새
`PhaseEntryPattern`도 한 페이즈에 동시에 지정하지 않는다.

## 패턴 선택과 예고 표시

`PatternSelection`의 의미:

| 값 | 실행 규칙 |
|---|---|
| Priority | GAS가 허용하는 첫 항목 선택 |
| Random | GAS가 허용하는 항목 중 서버에서 균등 선택 |
| Sequence | 배열 순서로 반복. 다음 항목이 불가하면 대기. 정상 종료 시 다음 항목으로 이동 |

Sequence는 취소/실패에서 현재 항목을 유지한다. 비용·쿨다운은 기존 GAS 규칙을 따른다.
페이즈 변경, 리셋 및 컨트롤러 교체는 순서를 초기화한다. 연속 공격은 각 GA 종료 후
BT의 다음 실행에서 이어지며, 애니메이션 내부의 타격 프레임이나 이동 연결은 GA가 담당한다.
같은 GA를 쓰는 항목은 기존 GA 쿨다운/태그를 공유한다. 서로 다른 쿨다운이 필요하면
별도 GA 변형과 태그를 작성한다.

`RPGBossDefinition.bShowTelegraphsByDefault`가 해당 프로필의 기본 표시 정책이다.
각 `Patterns` 및 `PhaseEntryPattern`의 `TelegraphPolicy`가 다음처럼 적용된다.

| 값 | 표시 |
|---|---|
| Inherit | 프로필 기본값 사용 |
| Show | 프로필 기본값과 관계없이 표시 |
| Hide | 프로필 기본값과 관계없이 숨김 |

예를 들어 Hard의 기본값을 false로 두고 반드시 안내할 기믹만 Show로 지정할 수 있다.
숨긴 공격도 동일한 시전 시간, 비용, 서버 HitQuery와 피해 처리를 수행한다.
이 정책은 **시전 중 공격 범위 데칼**에 적용한다. 애니메이션, 소리, 보스 머리 위의
GameplayCue, 이미 생성된 지속 장판의 효과는 각 연출/기믹의 계약을 따른다.
형상/머티리얼 작성은 [보스 데칼 형상 계약](BOSS_TELEGRAPH_SHAPES.md)을 따른다.

새 항목은 기존 BT의 `bUsePhasePatterns=true` 경로로 실행한다. 별도 서버 기믹은
`TryActivatePattern(PatternId, ExpectedEncounterId, ExpectedPhase)`를 호출한다.
항목 기반 공격을 GA 클래스만 직접 활성화하여 설정/표시 정책을 우회할 수 없다.
GA 내부의 예고는 `StartBossTelegraph` 또는 `StartBossTelegraphForHitQuery`를 사용한다.
직접 BossAI의 저수준 `BeginTelegraph`를 호출하면 프로필 기본값만 적용된다.

## 소환 기믹 조절과 확장

`RPGBossSummonAbility` 기반 항목의 `Settings`에는 인라인
`RPGBossSummonPatternSettings`를 지정할 수 있다. 배치 목록 `LocalSpawnOffsets`의
길이가 소환 수이며 1..8개를 허용한다. `WindupSeconds`와 `Lifetime`도 항목별로 지정한다.
소환 클래스, 내비게이션 사용, 최대 소유 수, 초기화 제한 시간 및 타깃 필요 여부는
기존 GA에 남는다. 소유 수 상한이나 좌표/시간 검사를 설정으로 완화할 수 없다.

`RequiredSummonDefeats=0`은 기존처럼 첫 성공한 웨이브 전원 처치다. 양수는 해당 웨이브의
서로 다른 구성원 중 N명 처치이며, 어느 슬롯을 처치해도 된다. 단순 despawn은 처치가 아니다.
`SummonWaveCompletionPhase`와 `TimeLimitSeconds`가 필요하며, 완료 시 기존 페이즈 전환이
남은 소환체를 정리한다. 요구 수가 실제 웨이브보다 많으면 성공으로 낮춰 보정하지 않으며
제한 시간 초과로 실패한다. 작성 시 요구 수와 소환 배치를 함께 확인해야 한다.

제한 시간 초과의 기본 결과는 기존 `Wiped`다. 새로운 실패 효과나 새로운 기믹 동작은
그 동작을 구현한 GA/native/BP가 필요하다. 이미 지원하는 동작은 GA 변형, 페이즈 데이터,
항목 설정만으로 재사용한다. 다른 기믹의 설정은 `RPGBossPatternSettings`의 별도 타입과
해당 GA의 `ValidatePatternSettings`로 추가하며, 지원하지 않는 타입은 정의 검증에서 거절한다.

## 책임과 서버 수명

- `RPGBossEncounterProfiles`: 난이도와 완전한 전투 정의의 대응 및 검증. 서버 GameMode의
  난이도를 읽으며 온라인 서버는 백엔드 확정 전 시작을 거절한다. 클라이언트의 입장 대기
  난이도나 UI 설정을 읽지 않는다.
- `RPGBossDefinition`: 페이즈·패턴·기믹 구성과 데이터 검증. 전투 시작 시 전체를 복제해 고정한다.
- `RPGBossPatternExecutor`: 서버의 항목 선택, 순서, GAS 실행 문맥 전달. BT는 실행 요청과
  자신이 시작한 GA의 완료/취소를 관리한다.
- `RPGBossGameplayAbility`: 시전 시작 시 항목 ID·표시 정책·타입별 설정을 고정하고,
  기존 서버 시전/타깃/피해 보안 계약을 유지한다. 구체적 GA가 기믹 동작을 구현한다.
- `RPGBossTelegraphPresentationComponent`: 복제된 현재 상태의 표시만 담당한다. 표시 여부,
  패턴 ID, 페이즈, 형상과 서버 시각을 같은 예고 스냅샷으로 받는다. 중도 참가/비동기
  머티리얼 로딩도 최신 상태를 읽으며 이전 페이즈의 예고는 표시하지 않는다.

실행 중 에셋 변경은 현재 전투/시전에 반영되지 않는다. 다음 전투 시작에서 다시 선택한다.
온라인 난이도는 기존 GameMode의 백엔드 확정 계약을 유지하며, 레벨/stat용 startup 능력의
재지급이나 클라이언트 난이도 결정 경로를 추가하지 않는다.

## 검증

유지할 자동화는 `ProjectRPG.Boss.Profiles.*`에 있다. 정의 오류/같은 GA의 별도 항목,
서버 프로필 선택·고정·기존 정의 호환, 실제 BT와 순차 실행/취소/리셋, 숨긴 시전의 실제 GAS 피해,
항목별 Show 예외 및 소환 설정/부분 처치 완료를 검사한다.
`RPGBossProfileTestGameMode`의 `-RPGBossProfileNetwork`는 별도 테스트 시나리오이며
실제 입장 인증이나 게임 보스 에셋을 바꾸지 않는다.

2026-11-05 검증은 게시된 `d0c0cc2`와 본 기능 소스 23개를 고정한 별도 프로젝트에서
수행했다. UHT, 변경 헤더를 사용하는 CPP와 생성 코드 79개, 모듈 링크를 통과했다.
변경 없는 객체는 입력과 정상 모듈 해시를 확인한 기존 빌드에서 재사용했으며,
전체 Editor 타깃의 재빌드 완료를 뜻하지 않는다. 분리 PCH의 디버그 타입 정보에 관한
LNK4206 경고가 있었고 컴파일/최종 링크 오류는 없었다.

같은 모듈로 신규 프로필 5개와 기존 보스 회귀를 포함한 네이티브 검사 21개가 통과했다.
선택 검사 이전 엔진 기동 중의 `Condition failed` 메시지는 기존 정상 모듈에서도
확인되는 별도 기록이며, 선택한 21개 검사의 오류 수는 모두 0이다.
Iris 전용 서버와 클라이언트 2개에서 80..160ms 지연·5% 패킷 유실을 설정하고
숨김 시전 도중 참가, Show 예외, 클라이언트의 상태 변경 거절, 숨긴 공격의 실제 피해
(HP 100→69), 전멸/재시작/사망/접속 종료를 확인했다. 소스 23개와 관련 에셋 7개는
검사 전후 동일했다. 이 검사는 NullRHI이며 새 GPU 렌더 검사, 실제 온라인 입장 인증,
난이도별 출시용 보스 데이터의 밸런스 검사를 포함하지 않는다.

실제 보스에 사용할 난이도별 프로필/정의 에셋은 위 작성 순서대로 별도 설정해야 한다.
이번 변경은 기존 보스의 패턴이나 난이도 수치를 임의로 바꾸지 않는다.
