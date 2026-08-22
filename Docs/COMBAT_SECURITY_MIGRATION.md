# 전투 보안 마이그레이션 현황

## 적용 완료

- 피해 GameplayEffect의 서버 전용 적용과 SetByCaller 피해량 상한 검증
- 서버 HitResult의 대상 일치, 적대 관계, 거리, 피격점 검증
- 레거시 플레이어 단일 타격과 AOE의 대상 중복 제거 및 수 제한
- Projectile `OnHit`/`OnBeginOverlap` 공통 서버 판정과 1회 충돌 처리
- Gladiator 근접, Projectile, AOE의 보안 프로필 및 타격 예산 적용
- 클라이언트 TargetData의 대상 수·거리·시야 서버 재검증
- 지면 지정 좌표의 서버 지면 Trace·거리·시야 재검증
- GameplayEffect 적용 실패 시 직접 Attribute 피해로 우회하던 경로 제거
- 대시·도약 같은 이동 불연속의 제한 시간·거리 예산 승인

## BP 설정 필요

보안 경계는 C++에 있지만 실제 제한값은 각 콘텐츠의 Class Defaults 또는 `RPGSkillDefinition.Security`에 맞춰야 한다.

- 레거시 플레이어 스킬: `Skill > Legacy > Security`
- `RPGProjectileBase` 파생 BP: `Projectile > Security`
- Gladiator 근접 Ability: `Maximum Server Damage Per Hit`
- Gladiator Projectile/AOE Element: `Security Profile`
- SkillContainer 기반 스킬: `RPGSkillDefinition.Security`

호환 기본값은 마이그레이션 중 기존 스킬을 보존하기 위한 값이다. Dedicated Server 플레이테스트에서 정상 최대값을 측정한 뒤 스킬별 상한을 낮춰야 한다.

## 검증 결과

- `Project_RPGEditor Win64 Development`: 빌드 성공
- 보안 관련 Blueprint 8개: 컴파일 오류 0, 실패 0
- `ProjectRPG.*` 자동화 테스트: 35 성공, 0 실패

검증한 BP는 레거시 기본 공격 1개, 플레이어 스킬 4개, NPC 근접 기본 Ability, Projectile Base, Glacer Projectile이다.

## 기존 콘텐츠 이슈

다음 항목은 이번 변경에서 만든 오류가 아니지만 전체 Blueprint 일괄 컴파일을 막고 있다.

- `Content/Blueprints/Character/NPC/Gruntling/BT_Guardian.uasset`: 패키지 이름 테이블이 파일 끝을 가리키는 손상 자산
- `Plugins/GameFeatures/GladiatorCore/Content` 일부 BP: `D1GameplayAbility_*`, `D1WeaponBase`, `D1ItemTemplate` 등 원본 부모 클래스 누락
- `GA_NPC_MeleeAttack_Base`: 현재 태그 테이블에 없는 `Enemy.Ability.Melee`, `Enemy.Ability`, `Enemy.Status.Unblockable` 참조

## 출시 전 남은 보안 작업

- Dedicated Server의 고핑·패킷 손실·프레임 드랍 조건에서 허용치 튜닝
- 위반 이벤트의 서버 텔레메트리 저장, 세션 단위 상관 분석, 운영자 조회 화면
- 거래·드롭·제작·보상 트랜잭션의 감사 로그와 재처리 도구 검증
- 속도핵·연사핵·사거리핵·TargetData 변조를 재현하는 네트워크 통합 테스트
- 필요 시 플랫폼 안티치트 연동. 플랫폼 안티치트는 서버 권위 검증을 대체하지 않고 보조한다.
