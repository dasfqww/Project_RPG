# RPG 코드 네이밍 원칙

## 의미별 접두 범위

- `Player`: 플레이어 입력, 카메라, 장비 요구 조건, 플레이어 스킬 수명주기처럼 플레이어에게만 필요한 코드
- `Combat`: 플레이어와 NPC가 공유하는 피해 적용, 투사체, AOE, 피격 검증
- `Ability`: 조종 주체와 무관한 GAS 기반 능력 및 타기팅 기반 클래스
- `Gladiator`: 실제 Gladiator 직업에만 적용되는 스킬, 데이터, 연출
- `Legacy` 또는 `D1 Compatibility`: 이전 D1 자산을 불러오기 위해서만 유지되는 호환 표면

## GameFeature 예외

`GladiatorCore` 플러그인 이름과 `/GladiatorCore` 콘텐츠 마운트 경로는 기존 `.uasset`의 패키지 정체성이다. 일반 소스 이름 정리와 달리 패키지 리다이렉트·자산 재저장·쿠킹 검증 없이 변경하지 않는다.

## reflected 타입 변경 규칙

`UCLASS`, `USTRUCT`, `UENUM` 이름은 BP와 DataAsset에 직렬화된다. 다음 조건을 모두 만족할 때만 이름을 변경한다.

1. 기존 이름에서 새 이름으로 Core Redirect를 추가한다.
2. D1 원본 이름의 Redirect도 새 타입을 직접 가리키게 한다.
3. 관련 BP를 열고 컴파일한 뒤 새 이름으로 재저장한다.
4. Editor 빌드, BP 허용 목록 컴파일, 쿠킹 검증을 통과한다.
5. Redirect 제거는 배포된 모든 자산이 재저장된 이후 별도 릴리스에서 수행한다.

## 현재 마이그레이션

- 플레이어 스킬·장비 Ability: `Ability/Player`
- 공용 Projectile/AOE Effect Actor: `Ability/Combat`
- 플레이어 조준 TargetActor: `Ability/Player`
- 기존 `RPGGladiator*` reflected 타입: 자산 재저장 단계 전까지 호환 이름 유지
