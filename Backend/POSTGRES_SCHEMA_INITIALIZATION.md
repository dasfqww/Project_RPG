# PostgreSQL 스키마 초기화 동시성

2026-10-08에 공유 개발 DB의 병렬 백엔드 기동 시 스키마 초기화를 직렬화했다.
2026-10-06 E2E에서 관측된 `EnsureCreatedAsync`의 `40P01` 교착을 순차 테스트만으로
해결됐다고 간주하지 않고, 실제 초기화 경로와 병렬 회귀를 보강한 작업이다.

## 장비 단계 델타와 버전2 — 2026-10-31

동결한 버전 1 `Database/schema.sql` 뒤에
[0002_item_equipment_progression.sql](Database/Migrations/0002_item_equipment_progression.sql)을 추가했다.
`item_records.equipment_progression`은 nullable JSONB이며 기존 행은 NULL로 유지한다.
형식·구간/버전·정수 단계 0~3·비중첩 수량을 CHECK로 제한한다. 버전 1의 SQL/해시는 바꾸지 않았다.

Production의 명시적 마이그레이션/읽기 전용 시작 검증은 현재 카탈로그의 두 버전을 요구한다.
Development/Testing과 직접 영향받는 테스트 fixture는 같은 전체 초기화 SQL을 기존 잠금/트랜잭션으로
재생하며 운영 배포 이력을 임의 생성하지 않는다. 2026-10-31 당시 실제 서비스나 기존 DB에는 적용하지 않았다.
새로운 코드/형식과 기존 릴리스의 혼합 운영을 검증한 것은 아니다.

자체 tmpfs PostgreSQL에서 버전 1→2·기존 행 보존/no-op·새 연결 재조회·CAS 경합·잘못된 값 거절·
전체 초기화 재생을 확인했다. 기존 Schema/Reward/Abandonment/픽업 PG 전체 suite와 제한 운영 역할/
실제 Production 웹 검사는 반복하지 않았다. [실행·정리·적용 경계](../Docs/PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#11-2026-10-31-backend-단계-보존과-버전2-마이그레이션)를 따른다.

## 기존 로컬 개발 DB의 v2 적용 — 2026-11-01

사용자 승인으로 기존 compose PostgreSQL의 `127.0.0.1:54329/project_rpg`에
기존 v2 SQL만 적용했다. 해당 DB에는 운영용 `rpg_schema_migrations`가 없으므로 전체
`--migrate-schema`/v1 재생이나 이력 테이블의 생성·수동 버전 표시는 하지 않았다.
개발 DB의 제한된 델타 적용이며 Production 배포 이력이 준비됐다는 뜻이 아니다.

현재 Windows 사용자·SYSTEM·Administrators만 허용한 최종 복구용 전체 custom-format
백업을 먼저 만들고 archive 목록과 컨테이너/호스트의 SHA256 일치를 확인했다.
기존 초기화와 같은 advisory transaction 잠금, 5초 lock timeout, 30초 statement timeout과
단일 transaction으로 적용했다. 전후 public 25테이블의 18,371행은 행 수·데이터 지문이
일치했다. 새 연결에서 nullable/no-default JSONB와 validated CHECK, 기존 아이템
10,759행의 새 값이 전부 SQL NULL임을 확인했다. 게임 데이터 DML은 실행하지 않았다.

초기 결과 보고의 JSON 줄바꿈 파싱 오류는 기존 전체 로그의 before/after/committed와
읽기 전용 재조회로 확인을 마쳤다. DDL은 반복하지 않았고 초기 실패 결과도 보존했다.
최종 근거는 `Saved/BuildChecks/DevDbV2-20261101/e13d2e7b8a484529b37b6238475fd414/FinalResult.json`이다.
복구 백업 `Saved/Backups/Database/DevV2-e13d2e7b8a484529b37b6238475fd414/Before-v2.dump`는
4,679,864 bytes로 보존하며 복원 리허설은 하지 않았다. 같은 용량의 컨테이너 임시 덤프는
finally에서 삭제했고 잔여·정리 예외는 없다. UE/실제 사망 복구 E2E·서비스 배포는 미실행이다.

## 초기화 계약

개발/`Testing` 기동의 `PostgresGameRepository.EnsureCreatedAsync`는
`PostgresSchemaMigrations.LoadInitializationSqlAsync`로 기준 스키마와 현재 버전별 SQL을 합쳐
`PostgresSchemaInitializer.ApplyAsync`에 전달한다. 2026-10-31 현재 버전 1은 기존
`Database/schema.sql`, 버전 2는 별도 장비 단계 컬럼 SQL이며 기준 파일은 동결 상태다.
초기화는 다음 순서를 따른다.

1. 연결을 열고 명시적 트랜잭션을 시작한다.
2. 스키마 SQL 실행 전에 `pg_advisory_xact_lock(integer, integer)`를 획득한다.
3. 같은 연결·트랜잭션으로 스키마 전체를 적용한다.
4. 커밋 후에만 저장소의 스키마 준비 완료를 기록한다.

잠금 키는 `(0x52504753, 0x5343484D)`이며 프로세스·릴리스마다 바꾸지 않는다.
초기화 대상 테이블이 아직 없는 새 DB에서도 사용할 수 있고, 같은 DB의 협력하는
초기화 호출을 직렬화한다. 서로 다른 테스트 스키마도 같은 DB에서는 이 잠금을 공유한다.
기존 아이템 트랜잭션의 단일 `bigint` 키 공간과 별개의 두 `integer` 키 공간을 사용한다.
키 공간의 분리와 잠금 함수는 [PostgreSQL 17 문서](https://www.postgresql.org/docs/17/functions-admin.html#FUNCTIONS-ADVISORY-LOCKS)를 따른다.

커밋 전 SQL 오류·호출 취소는 트랜잭션 폐기 시 롤백하며 잠금도 트랜잭션 종료에 따라
해제된다. 세션 수준 잠금을 연결 풀에 남기거나 오류를 무시하고 서비스하지 않는다.
이 동작은 [PostgreSQL 트랜잭션 수준 잠금](https://www.postgresql.org/docs/17/explicit-locking.html#ADVISORY-LOCKS)과
[Npgsql 트랜잭션](https://www.npgsql.org/doc/basic-usage.html#transactions)을 사용한다.

잠금 대기와 스키마 실행 모두 연결 문자열의 Npgsql command timeout 및 호출자의 취소를
유지한다. timeout을 무제한으로 바꾸거나 무조건 재시도하지 않는다. `Program.cs`는
초기화가 성공한 뒤 웹 호스트를 실행하므로 초기화 실패를 준비 완료로 표시하지 않는다.
현재 시작 호출은 `CancellationToken.None`을 사용한다. 테스트의 호출 취소 검증을
운영 프로세스 종료 신호와의 연동 완료로 확대해서 해석하지 않는다.

스키마 파일은 기존 내용을 유지했다. 로스터 backfill, 캐릭터 연결, 제약 변경,
이전의 세션에 묶이지 않은 Join Ticket 무효화 등 기존 업그레이드 동작도 그대로다.
파일은 신뢰된 배포 SQL이며, 별도 `BEGIN`/`COMMIT`이나 트랜잭션 밖에서 실행해야 하는
명령을 추가하려면 초기화 계약을 다시 설계해야 한다.
보상 저장소 회귀의 직접 SQL 초기화도 같은 helper를 사용한다.

## 회귀 실행

개발 PostgreSQL이 실행 중이어야 한다. 기본 연결은 기존 로컬 컨테이너의
`127.0.0.1:54329/project_rpg`이며 DB나 컨테이너를 새로 만들거나 지우지 않는다.

```powershell
dotnet run --project Backend/ProjectRpg.Backend.SchemaTests/ProjectRpg.Backend.SchemaTests.csproj
```

다른 로컬 개발 연결을 지정하려면 다음 환경 변수를 사용한다. 연결 문자열을 로그나
저장소에 기록하지 않는다. 원격 호스트는 거절한다.

```powershell
$env:PROJECT_RPG_SCHEMA_TEST_POSTGRES = $env:POSTGRES_CONNECTION_STRING
dotnet run --project Backend/ProjectRpg.Backend.SchemaTests/ProjectRpg.Backend.SchemaTests.csproj
```

테스트마다 `rpg_schema_test_<GUID>` 전용 스키마와 명시적 search path를 만든다.
`public`의 플레이어·보상 데이터는 수정하지 않는다. 종료 시 생성한 정확한 이름과
현재 사용자 소유권을 검증한 뒤 그 테스트 스키마만 정리한다. 따라서 개발 계정에
테스트 스키마 생성·삭제 권한이 필요하다. 운영 DB에서 실행하는 테스트가 아니다.
자식 프로세스도 이 search path를 강제하며 자체 생성한 프로세스만 종료한다.

10개 회귀가 검사하는 범위:

- 시작 장벽을 공유하는 별도 4개 프로세스의 새 스키마 초기화 및 전체 테이블 생성.
- 실제 백엔드 웹 호스트 4개가 같은 새 스키마로 기동하고 각각 liveness/readiness 통과.
  HTTP 포트는 운영체제가 할당한 별도 loopback 포트이며 Steam 인증은 사용하지 않는다.
- 독립 연결 풀 8개의 기존 스키마 동시 초기화 3회 및 데이터 보존.
- 별도 4개 프로세스 재기동 후 계정·캐릭터·로스터·현재 인증 세션 보존.
- 기존 잠금을 가진 동안 `pg_locks`로 초기화 대기를 관측하고 DDL 미실행 확인.
- 대기 중 취소 후 단일 연결 풀 재사용 및 재초기화.
- SQL 오류의 DDL 롤백·잠금 해제·재초기화.
- 실행 중인 스키마 SQL 취소의 DDL 롤백·잠금 해제·단일 연결 풀 재사용.
- 1초 command timeout의 대기 제한·DDL 미실행·재초기화.
- 로스터가 없는 이전 스키마를 4개 프로세스로 업그레이드하고 기존 캐릭터 유지.

잠금 해제 검증은 새로운 물리 연결에서 수행한다. 같은 연결에서의 재진입 성공을
잠금 해제 성공으로 오인하지 않는다.

## 검증 기록 (2026-10-08)

작업 로그 디렉터리:
`Saved/Tests/PostgresSchema/0553dfa5b3a64f5ca8eb72fbb4b455c0/`.

- `build-final.log`, `reward-build.log`, `health-build.log`: .NET Debug 빌드 성공,
  각각 경고·오류 0개. 프로젝트별 별도 build artifacts 경로를 사용했다.
- `schema-tests-final.log`: 초기화 10개 성공. 실제 웹 호스트 4개 및 별도 프로세스의
  병렬 초기화/재기동/업그레이드 포함. 생성한 세 테스트 스키마 정리 완료.
- `reward-tests.log`: 권한 2개, 메모리 보상 25개, PostgreSQL 보상 25개, 총 52개 성공.
  인벤토리 우선·초과 우편·원자성·동시 지급·멱등 재시도 회귀 포함.
- `health-tests.log`: DB 장애/지연·풀 포화·취소·복구 6개 성공. 기존 컨테이너를
  중지하거나 실제 DB 장애를 주입하지 않고 전용 loopback proxy를 사용했다.
- `integration-result.json`: 기존 전체 PostgreSQL HTTP 통합 성공. 실행 ID
  `792cf117c826`, 테스트 포트 `3031/TCP`. 백엔드 재시작 후 인증·스키마 재적용·보상·
  보안 이벤트 유지, 네 난이도의 이전 우편 전용/인벤토리 우선 8개 보상 사례 통과.
  원본 빌드/서버 로그는 `C:/Users/User/AppData/Local/Temp/project-rpg-postgres-652d5257f93d4cf6ae3aeee615618a75/`에 보존했다.
- 로그 성공 마커를 별도로 확인해 초기화 10개·보상/권한 52개·DB 장애 6개,
  총 68개 및 실패 마커 0개를 확인했다. HTTP 통합의 두 서버 실행 모두 스키마 준비
  완료를 기록했고 `40P01`/교착/Unhandled exception이 없었다.
- 예비/최종 회귀에서 생성한 정확한 다섯 스키마의 잔존 수 0개, 테스트 소유
  dotnet 프로세스 및 `3031/TCP` 리스너 종료를 확인했다. 기존 PostgreSQL은 계속 실행 중이다.

테스트용 스키마 외 기존 개발 데이터는 삭제하지 않았다. 전체 HTTP/보상 회귀가 생성한
고유 테스트 계정·보상 기록과 실행 로그는 보존한다. UE 소스/에셋은 변경하지 않았으며
이번 검증을 UE 재빌드·실제 Steam·자연 전투·Shipping 검증으로 표시하지 않는다.

## 운영 범위와 남은 작업

- Advisory lock은 협력하는 초기화 호출 사이의 잠금이다. 이전 바이너리나 수동 SQL이
  이 잠금을 사용하지 않으면 보호되지 않는다. 모든 초기화 경로가 같은 키/트랜잭션
  계약을 사용하도록 배포해야 한다.
- 일반 플레이어 요청·정산을 이 advisory lock에 묶지 않았다. 스키마 변경 자체의
  테이블 잠금은 여전히 존재한다. 이번 수정은 모든 런타임 쿼리 교착을 없애는 작업이 아니다.
- 아래 2026-10-30 후속에서 운영 기동과 버전 관리 배포 명령을 분리했다. 개발/`Testing`의
  전체 DDL 재적용은 유지한다. 실제 배포 역할의 권한 설정/실검증과 무중단 rolling upgrade,
  구·신 버전 스키마 호환성은 여전히 별도다.
- 커밋 응답 중 연결이 끊기면 커밋 결과가 불확실할 수 있다. 적용 SQL의 멱등성과
  운영 진단이 필요하며 네트워크 오류를 자동 성공으로 처리하지 않는다.
- 멀티 호스트/운영 규모의 부하·장애 검증은 이번 로컬 PostgreSQL 회귀와 구분한다.

## 운영 기동과 버전 관리 배포 명령 (2026-10-30)

`Program.cs`의 PostgreSQL 기동을 다음처럼 분리했다. 기존 서비스에 배포한 결과가 아니라
새 소스의 동작이며, 최초 전환에는 사전 배포 작업이 필수다.

| 경로 | 스키마 동작 | 웹/정산 워커 |
|---|---|---|
| `--migrate-schema` | 별도 배포 계정으로 누락된 버전 SQL과 이력을 한 트랜잭션에 적용 | 시작하지 않고 종료 |
| 일반 기동, Development/Testing 이외 환경 | 이력 SELECT와 현재 릴리스의 버전·이름·SHA256 비교만 수행 | 일치한 뒤에만 시작 |
| 기존 Development/Testing 일반 기동 | 기존 `EnsureCreatedAsync`의 전체 SQL 적용 유지 | 적용 성공 후 시작 |

운영 일반 기동은 이력 테이블이 없거나 적용이 덜 됐거나, 이미 적용된 SQL/이름이 달라졌거나,
현재 바이너리보다 DB 버전이 높으면 실패한다. 자동 적용·이력 stamp·추측 복구는 하지 않는다.
`ValidateAsync`에는 DDL/이력 쓰기/advisory lock이 없으며, 전체 물리 스키마의 drift를
검사하는 도구는 아니다. 이력만 수동으로 작성/변경하지 않는다.

`PostgresSchemaMigrations`는 현재 `Database/schema.sql`을 **버전 1 / baseline**으로 읽는다.
처음 배포된 뒤 이 SQL을 수정하지 않는다. 후속 변경은 별도 SQL 파일을 패키지에 포함하고
코드의 카탈로그에 2, 3, … 순서로 추가한다. 순서 누락/중복 이름/빈 카탈로그는 연결 전에
거절한다. SQL 해시는 UTF-8 및 LF로 정규화해 Windows/Git의 줄바꿈 차이는 제외한다.
내용/이름이 다른 적용 이력이나 미래/불연속 이력은 거절하고 재적용하지 않는다.

배포 명령은 기존 초기화와 **같은 advisory lock 키**를 먼저 획득하고,
`rpg_schema_migrations(version, name, sha256, applied_at)` 이력과 필요한 SQL 전체를
같은 연결·트랜잭션에서 처리한다. 실패/취소는 새 DDL과 이력을 함께 롤백한다.
이미 적용된 버전은 실행하지 않는다. 비버전 관리 기존 DB의 첫 전환도 baseline을 실제로
적용한 뒤 기록하며, 기존 DB라는 이유로 적용 없이 stamp하지 않는다. 신뢰된 배포 SQL에
자체 `BEGIN`/`COMMIT`이나 트랜잭션 밖 실행 명령을 넣지 않는 기존 계약을 유지한다.

### 배포 순서와 권한 경계

1. 백업/복구 계획을 확인하고 기존 API·정산·게임 서버의 DB 변경을 중지/배출한다.
   기존 바이너리의 자동 초기화와 섞어 기동하지 않는다. 첫 baseline은 기존 backfill/제약/
   오래된 티켓 정리 SQL을 실제 실행하므로 온라인 무영향 작업이 아니다.
2. 배포 비밀 저장소에서 **배포 역할**의 연결 문자열과 명시적인 대상 DB/search path를 공급하고
   같은 릴리스의 `dotnet ProjectRpg.Backend.dll --migrate-schema`를 실행한다.
   실패/커밋 응답 유실을 성공으로 간주하지 않는다. 동일 릴리스 재실행은 커밋된 이력으로 판단한다.
3. **별도 런타임 역할**의 연결 문자열로 바꿔 일반 서버를 시작한다. 런타임 역할은 DB/스키마/
   테이블의 소유자가 아니어야 하며 CREATE/ALTER/DROP 권한을 주지 않는다. 게임 테이블의
   필요한 DML/SELECT·스키마 USAGE 및 이력 테이블의 SELECT만 허용하고 이력 쓰기는 금지한다.
   이 코드가 계정/GRANT를 자동 생성하거나 기존 계정 권한을 회수하지는 않는다.
4. readiness 및 실제 플레이 흐름을 확인한 뒤 트래픽을 연다. 운영을 Development/Testing으로
   실행하면 기존 자동 DDL 경로가 켜지므로 운영 환경 이름과 비밀 주입을 고정한다.

현재 정책은 **릴리스와 정확히 일치하는 이력**만 허용한다. 신버전 DB에서 구버전 서버를
동시에 돌리는 rolling upgrade/다운그레이드는 지원 완료가 아니다. 유지보수 전환이 기본이며
추후 expand/contract SQL과 명시적 호환 버전 범위·부하/잠금 분석을 별도로 설계한다.
일반 요청과 migration의 테이블 잠금 경합, 프로세스 종료 신호와 CancellationToken 연동도
이 변경만으로 해결되지 않는다.

### 집중 검사와 남은 실검증

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-PostgresMigrationTests.ps1
# 기존 loopback PostgreSQL을 사용할 수 있을 때만 명시적으로 추가:
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-PostgresMigrationTests.ps1 -Postgres
```

러너는 자체 소스 사본/Release artifacts에서 빌드하며 실행 후 성공/실패 finally에서 Temp를
폐기한다. 작은 입력/모듈 해시·명령·필수 로그만 보존하며 폐기 DLL을 그대로 재실행할 수는 없다.
DB 옵션은 `PROJECT_RPG_SCHEMA_TEST_POSTGRES` 또는 기존 loopback 개발 연결만 허용한다.
생성한 `rpg_migration_test_<GUID>` 스키마만 소유권을 확인해 정리하고 public 데이터·기존
컨테이너/서버를 중지하지 않는다.

2026-10-30 결과: 새 테스트 fixture의 local 함수 참조 오류 2개로 최초 빌드가 실패했고
테스트 클래스 내부의 검사만 바로잡았다. 최종 분리 Release 빌드는 경고/오류 0,
계획/해시/버전 경계·실제 명령의 Memory 거절(개발/운영)·중복 인수 거절 7그룹과 종료 0이다.
입력 96개 및 검증 DLL 2개의 해시를 기록했고 실행 중 원본 입력은 같았다.
두 시도의 Temp 합계 14,574,707 bytes(13.90 MiB)를 자동 폐기했으며 정리 예외는 없다.
기록: `Saved/BuildChecks/PostgresVersionedStartup-20261030/8060a8f4048a4c4e92f2a3c5f2f07c25/Result.json`.

이때 로컬 PostgreSQL 리스너와 Docker 엔진이 꺼져 있어 임의 기동하지 않았다.
추가 작성한 실제 PG의 baseline/병렬 delta/비멱등 SQL 재적용 방지/읽기 전용 검증/
SQL·이력 롤백/실제 배포 명령 검사는 **미실행**이다. 실제 제한된 DB 역할로 운영 기동,
기존 DB 첫 전환·보상/아이템/접속 데이터 보존, publish 배치와 운영 배포도 미검증이다.
기존 초기화/보상/UE·네트워크/600초 suite를 반복하지 않았다.

## 격리 PostgreSQL·제한된 운영 역할의 실제 연결 (2026-10-30)

위 최초 소스 구현 시의 PG 미실행 항목 중 아래 경계를 실제 PostgreSQL에서 연결했다.
Docker Desktop은 숨김 기동했고 기존 DB에 접속하지 않았다. 캐시된 `postgres:17-alpine`만
사용하며 신규 이미지 pull·호스트 디렉터리/기존 volume mount 없이 자체 이름/label의
컨테이너, 512MiB 메모리/1 CPU 제한, OS가 배정한 loopback 포트와 tmpfs DB를 사용했다.
검증 종료 후 이 컨테이너만 제거했고 Docker 엔진·기존 이미지/컨테이너는 유지했다.

```powershell
# Docker 엔진과 캐시된 postgres:17-alpine 필요. 기존 개발 DB는 사용하지 않는다.
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-PostgresMigrationTests.ps1 -IsolatedPostgres
# 이미 지정한 loopback DB에서 기존 PG 사례만 실행(역할/운영 호스트 검사는 제외):
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-PostgresMigrationTests.ps1 -PostgresOnly
```

`--postgres-only`는 앞서 완료한 비DB 7그룹을 반복하지 않는다. 격리 모드만 임시 로그인 역할과
실제 Production 호스트 검사를 허용한다. 임시 역할은 직접 로그인하고 superuser/CREATEDB/
CREATEROLE/스키마 CREATE 권한이 없으며 테이블 소유자가 아니다. 게임 테이블의 DML은 허용하고
이력은 SELECT만 허용해 **전역 read-only 연결을 운영 역할로 대체한 시험이 아니다.**

분리 Release 빌드 경고/오류 0, PG 집중 **8그룹·검사 호스트 종료 0**:

- 비멱등 baseline 최초 1회/재실행 no-op 및 4개 동시 적용의 후속 버전 합계 1회, sentinel 보존.
- 실제 read-only 연결의 검증 SELECT, 변경된 SQL 및 미래 이력 거절.
- 기존 버전이 있는 상태의 후속 SQL 오류와 새 DDL/이력의 원자적 롤백.
- 이력 누락 시 검증 경로의 무변경과 최초 적용 실패 시 이력 테이블까지 롤백.
- 실제 `--migrate-schema` 프로세스의 패키지 baseline 적용·웹을 시작하지 않은 정상 종료.
- 실제 비버전 관리 스키마의 기존 계정/로스터/캐릭터/인증·가방/우편 아이템/재화 행을 넣고
  baseline을 적용해 JSON 행 스냅샷/현재 인증 보존. 아이템 revision·재화에 2^53 초과 정수 포함.
- 제한된 별도 DB 로그인으로 실제 Production 웹 호스트가 동적 포트의 readiness 200을 반환.
  CREATE/이력 UPDATE/새 migration은 `42501`로 거절되지만 게임 데이터 DML은 허용되며,
  실제 호스트 기동 전후의 플레이어 데이터/이력 스냅샷이 같다.
- 실제 Production 기동의 이력 누락/해시 변경/미래 버전은 웹 리스너 시작 전에 거절.

성공한 웹 호스트는 시험 소유 프로세스로 확인 후 계획적으로 종료했다. 검사 호스트의 종료 0과
웹 호스트의 자연 종료/우아한 drain 성공을 혼동하지 않는다. 역할/스키마 정리는 실제 로그에
남겼고 공유/운영 DB의 사용자나 권한을 변경하지 않았다.

입력 **97개**와 검증 DLL 2개 해시를 기록했고 원본 입력은 실행/완료 후 같았다.
Temp의 입력/Release DLL/PDB·빌드 사본 **9,073,309 bytes(8.65MiB)**와 자체 컨테이너/
tmpfs DB를 자동 폐기했다. 잔여 컨테이너/자기 dotnet 프로세스/Temp·정리 예외 0이다.
정리 직전 tmpfs의 `pg_database_size` 8,558,259 bytes는 DB 논리 크기 관측이며
호스트 디스크 삭제 용량에 합산하지 않는다. 작은 증거만 남겼으므로 같은 DLL 재실행/고정
입력 재감사는 불가하며 새 실행에는 새 빌드가 필요하다.

기록: `Saved/BuildChecks/PostgresVersionedStartup-20261030/fe57aa1900cf49f0abd936a780c6f11a/Result.json`
및 `Build.log`/`Tests.log`. 전체 live 검사 후에는 PS5의 `docker inspect` 미존재 오류가
정리를 오인 실패시키는 경계를 발견해, 자체 이름의 목록 조회→존재 시 label/ID 검증으로
바꿨다. 실제로 이미 폐기된 자기 컨테이너의 무예외 null 조회와 PS 구문을 확인했다.
이 마지막 helper 보완을 전체 PG 8그룹 재실행으로 표시하지 않는다.

게임/백엔드 제품 코드는 이번 회차에 바꾸지 않았다. 기존 공유 UE DLL·UBT/Intermediate와
열린 사용자 에디터를 유지했고, 실제 운영 DB의 전환·역할 provisioning/비밀 주입·publish 배치,
UE/보상 정산 전체 연결·Steam, 취소/커밋 응답 유실, rolling upgrade는 별도로 남는다.
