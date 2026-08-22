# Legacy Item Definition Adapter

## 목적

기존 Pickup Blueprint의 `FItemManifest`를 즉시 제거하거나 자산을 일괄 변환하지
않고, 새 아이템 시스템의 Definition 조회 계약으로 노출한다. 기존 Inventory/UI는
계속 동작하면서 신규 서버 트랜잭션과 읽기 모델이 같은 아이템 식별자를 사용할 수
있다.

```text
ARPGPickUpBase CDO
  -> FItemManifest
     -> FRPGLegacyItemDefinitionAdapter
        -> FRPGItemDefinitionView
           -> FRPGItemDefinitionRegistry
              ├─ IRPGItemDefinitionCatalog      (transaction snapshot)
              └─ IRPGItemDefinitionViewCatalog  (UI/presentation view)
```

`UDataManager::RefreshItemCache`는 기존 Manifest 캐시를 만들 때 같은 Manifest를
Adapter에도 전달한다. 따라서 기존 `GetItemManifestByTag` 호출은 그대로 유지되고,
신규 코드는 `GetItemDefinitionCatalog`, `GetItemDefinitionViewCatalog`,
`TryGetItemDefinitionViewByTag`를 사용할 수 있다.

## 안정 Definition ID

Legacy Manifest와 새 `URPGItemDefinition`은 모두 Item GameplayTag로 Definition ID를
만든다.

```text
RPGItemDefinition:GameItem.Craft.Fruit
```

자산 파일명이나 Blueprint 클래스명이 바뀌어도 영속 Record의 Definition ID는
유지된다. 같은 ItemTag의 새 `URPGItemDefinition`을 등록하면 Legacy 뷰를 교체한다.
이후 Legacy 자산 스캔이 다시 실행돼도 새 Definition을 덮어쓰지 않는다.

## 변환 범위

현재 Adapter는 다음 값을 변환한다.

- ItemTag -> 안정 Definition ID
- DefinitionVersion -> Legacy 시작 버전 1
- ItemCategory
- `FStackableFragment.MaxQuantity` -> MaxStackSize
- 이름 Fragment -> DisplayName
- 아이콘 Fragment -> Icon
- Legacy에 값이 없으면 GridSize는 `(1, 1)`

이름 Fragment가 없으면 ItemTag의 마지막 세그먼트를 표시 이름으로 사용한다.
유효한 ItemTag 또는 Inventory Category가 없는 Manifest는 등록하지 않는다.

## 인터페이스 분리

- `IRPGItemDefinitionCatalog`는 서버 트랜잭션에 필요한 ID, 버전, 최대 스택만
  반환한다.
- `IRPGItemDefinitionViewCatalog`는 UI와 Pickup 표시용 데이터를 반환한다.
- `FRPGItemDefinitionRegistry`가 두 인터페이스를 구현하지만 소비자는 필요한 작은
  계약에만 의존한다.
- 권위 스냅샷을 갱신하면 동일 ID의 표시 뷰 안 스냅샷도 함께 갱신해 버전과 규칙이
  어긋나지 않게 한다.

## 아직 Legacy에 남은 책임

Adapter는 읽기 경계만 제공한다. 다음 책임은 아직 기존 코드에 남아 있다.

- `FItemManifest::Manifest`의 `URPGItemBase` 생성
- Fragment의 Widget 직접 조립
- Pickup 스폰과 ObjectManager 접근
- 장착·소비 Fragment의 직접 실행
- 기존 Inventory UI의 `URPGItemBase` 참조

다음 단계는 장착·소비 명령을 서버 트랜잭션으로 옮기고, Projection 기반 UI가
`IRPGItemDefinitionViewCatalog`를 사용하도록 전환하는 것이다.

## 검증

- Legacy Manifest -> 안정 Definition View 변환 테스트
- Legacy와 Native Definition의 동일 ID 테스트
- Native Definition 우선순위 및 재스캔 불변성 테스트
- 권위 스냅샷과 표시 뷰의 버전·스택 규칙 일관성 테스트
- 전체 `ProjectRPG.Item` 자동화 테스트 15개 성공
