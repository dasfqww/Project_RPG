# RPG 보안과 Blueprint 연결 가이드

이 프로젝트의 보안 경계는 **서버 C++**, 밸런스와 연출은 **DataAsset/BP**가 담당한다. 클라이언트 BP가 제출한 피해량, 피격 목록, 위치를 그대로 신뢰하지 않는다.

## 1. 전역 정책 설정

1. Content Browser에서 `Miscellaneous > Data Asset`을 선택한다.
2. `RPGSecurityPolicy` 타입으로 `DA_SecurityPolicy_PvE`를 만든다.
3. 플레이어 Character BP의 `SecurityValidationComponent`에 이 에셋을 지정한다.
4. PvP가 생기면 별도의 `DA_SecurityPolicy_PvP`를 만들고 GameMode가 서버에서 `Set Security Policy`를 호출한다.

주요 항목:

- `Movement`: 서버 이동 샘플 간 거리·속도·불연속 허용치
- `Ability`: 초당 어빌리티 활성화 수와 동일 어빌리티 최소 간격
- `Combat`: 모든 스킬에 적용되는 최종 피해량 상한
- `Scoring`: 위반 점수 감쇠와 경고 임계값

`On Violation Reported`와 `On Risk Threshold Exceeded`는 서버에서만 바인딩한다. 자동 차단은 오탐 검토 전까지 바로 연결하지 않고, 우선 서버 로그·운영 텔레메트리에 기록한다.

## 2. 스킬 DataAsset 설정

각 `RPGSkillDefinition`의 `Security` 섹션에서 다음을 지정한다.

- `Maximum Server Hit Distance`: 시전자에서 대상 서버 바운드까지의 절대 상한
- `Hit Location Tolerance`: 서버 충돌 바운드와 피격점의 허용 오차
- `Maximum Targets Per Query`: 한 번의 서버 HitQuery 결과 상한
- `Maximum Hits Per Activation`: 한 번 활성화 중 누적 적용 가능한 타격 상한
- `Maximum Damage Per Hit`: 트라이포드·모드 배율 적용 후 1회 피해 상한
- `Authorized Movement`: 대시·도약·텔레포트에만 활성화하는 시간/추가 거리 예산

Content Browser의 `Validate Assets`를 실행하면 유효하지 않은 보안 프로필은 에러로 표시된다.

## 3. 공격 Ability BP 연결

피해가 발생하는 AnimNotify 또는 실행 이벤트에서 `Execute Authorized Skill Damage`를 호출한다.

입력:

- `Authorized Damage Effect Class`: 실제 체력 감소 GameplayEffect
- `Base Damage`: 서버가 알고 있는 스킬 기본 피해
- `Set By Caller Damage Tag`: 비워 두면 `Shared.SetByCaller.BaseDamage`

서버는 현재 활성화의 고정된 RuntimeSpec을 사용해 HitQuery를 다시 수행하고, 대상 수·거리·누적 타수·최종 피해 상한·적대 관계를 검증한다. `Out Applied Hits`는 검증을 통과해 실제 적용된 결과이므로, 서버 연출 복제나 후속 효과의 기준으로 사용한다.

다음 노드를 게임플레이 피해 경로에 직접 사용하지 않는다.

- 클라이언트가 만든 HitResult를 입력한 `Apply Gameplay Effect to Target`
- 클라이언트 계산 피해량을 받는 Server RPC
- 클라이언트 Overlap 결과를 그대로 받는 Server RPC

일반 NPC/보스처럼 SkillContainer 밖에서 서버가 직접 만든 HitResult를 처리할 때만 `Apply Authorized Server Damage`를 사용한다.

기존 BP 호환 경로도 서버 권위로 보강되어 있다. `Shared.SetByCaller.BaseDamage` 또는 `SetByCaller.BaseDamage`가 들어간 GameplayEffectSpec은 서버 전용 적용 함수에서 피해 스펙으로 인식되어 적대 관계·거리·피격점·피해 상한을 검사한다. 다만 새 BP는 대상 Actor만 넘기는 호환 노드보다 서버 Trace의 `HitResult`를 사용하는 승인 피해 노드를 우선한다.

레거시 플레이어 Ability는 Class Defaults의 `Skill > Legacy > Security`를 스킬별로 조정한다.

- `Legacy Server Direct Hit Distance`: 근접 공격의 서버 허용 사거리
- `Legacy Server Hit Location Tolerance`: 충돌체와 피격점 간 허용 오차
- `Legacy Maximum Targets Per Damage Event`: 회전베기 등 한 이벤트의 최대 대상 수
- `Legacy Maximum Damage Per Hit`: 해당 스킬의 실제 최고 피해보다 조금 높은 상한

호환 기본값은 기존 콘텐츠가 즉시 깨지지 않도록 넓게 잡혀 있다. 출시 값으로 그대로 사용하지 않는다.

## 4. 이동 Ability BP 연결

대시·도약 Ability에서 `Authorized Skill Movement Window` 태스크를 시작한다.

- `On Authorized`: 몽타주, Root Motion Source 또는 서버 이동을 시작
- `On Finished`: 정상 종료 후속 처리
- `On Cancelled`: Ability 취소 시 연출 정리
- `On Rejected`: 잘못된 프로필 또는 누락된 서버 보안 컴포넌트 처리

태스크는 클라이언트에서는 예측 연출만 시작하고, 서버에서만 제한된 시간과 총 추가 거리 예산을 연다. 완료·취소 시 예산은 자동 회수된다. 일반 텔레포트 BP 노드 앞뒤에서 수동 허가를 열어 둔 채 방치하지 않는다.

## 5. Projectile과 지속형 공격 BP 연결

`RPGProjectileBase` 파생 BP는 Class Defaults의 `Projectile > Security`를 반드시 설정한다.

- 직선 투사체는 실제 최대 비행 거리와 서버 지연 여유를 합친 값으로 `Maximum Server Hit Distance`를 지정한다.
- 투사체 반경보다 조금 큰 값으로 `Hit Location Tolerance`를 지정한다.
- 치명타와 서버 버프를 포함한 정상 최대값보다 조금 큰 값으로 `Maximum Damage Per Hit`를 지정한다.

충돌과 피해 적용은 서버 복제본만 담당한다. 클라이언트 복제본은 피격 연출만 표시하며, `OnHit`와 `OnBeginOverlap` 모두 같은 서버 검증 경로를 사용한다. 한 투사체는 첫 유효 충돌만 처리한다.

Gladiator 계열은 다음 위치도 함께 설정한다.

- 근접 Ability의 `Maximum Server Damage Per Hit`
- Projectile Element의 `Security Profile`
- AOE Element의 `Security Profile`

Projectile과 AOE Element는 활성화별 타격 수와 중복 대상을 서버에서 제한한다. GameplayEffect가 면역 또는 적용 조건 때문에 거절되면 직접 Attribute 감소로 우회하지 않는다.

## 6. TargetData와 지면 지정 스킬

클라이언트가 보낸 TargetData는 조준 의도일 뿐 판정 결과가 아니다. 서버는 다음을 다시 검사한다.

- 단일 대상 스킬: 대상 수, 유효 Actor, 최대 거리, 서버 시야 Trace
- 지면 지정 스킬: 유한 좌표, 최대 거리, 서버 지면 Trace, 최종 위치까지의 시야

검증 실패 시 스킬 효과와 스폰을 수행하지 않는다. TargetData가 없는 자기 중심 AOE만 서버의 시전자 위치를 안전한 기본값으로 사용한다.

## 7. 출시 전 운영 원칙

- Dedicated Server에서 패킷 지연·손실·고핑 환경을 포함해 허용치를 측정한다.
- `Risk Threshold` 초과는 곧바로 영구 밴하지 않고 세션 격리, 추가 로깅, 운영 검토 순으로 사용한다.
- 아이템·재화·거래는 기존 트랜잭션 서비스와 서버 저장소를 통해서만 변경한다.
- 치트 대응은 서버 권위 외에도 서버 로그 집계, 재현 가능한 감사 기록, 패치 가능한 정책 DataAsset을 함께 운영한다.
