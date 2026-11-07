# 백업 상태 수집과 외부 경보

2026-11-08. Linux DB 호스트의 pgBackRest 실행/상태 수집과 외부 monitor의 읽기/판정을 분리했다. 실제 복구·배포·운영 크기 RPO/RTO 달성 검증은 별도다. 게임 backend와 운영 웹에는 백업 실행·DB 복원 권한이나 버튼을 추가하지 않는다.

## 구성과 책임

- [backup_status.py](../Scripts/Operations/backup_status.py): Linux Python 3 표준 라이브러리 어댑터. pgBackRest의 고정 backup/info 명령과 PostgreSQL 읽기 전용 상태 조회만 실행한다. DB/원격 저장소 자격은 DB 호스트에만 둔다.
- [BackupPolicy](../Backend/ProjectRpg.Operations.Core/Monitoring/BackupStatus.cs): I/O 없는 상태 검증·경보 정책. backend 장애 중에도 백업을 독립적으로 평가한다.
- [FileBackupStatusReader](../Backend/ProjectRpg.Operations.Monitor/FileBackupProbe.cs): 관리 호스트의 로컬 읽기 전용 JSON을 8KiB 한도로 읽는다. 없음/손상/권한 실패/링크/필수 필드 누락은 Unknown이다.
- 기존 monitor의 사건/영속 outbox/HTTPS webhook을 재사용한다. 같은 eventId 재전송, 30분 지속 요약, 정상 표본 두 번의 복구를 보존한다.

## DB 호스트 배포 입력

pgBackRest 2.36 이상(JSON backup.error 제공), Python 3.9 이상, PostgreSQL client(psql)가 필요하다. 지원 대상은 Linux PostgreSQL 17이다. pgBackRest 설치 버전은 실제 배포에서 고정한다. stdout/stderr 원문을 상태 파일이나 monitor에 복사하지 않는다.

| 환경 변수 | 값 |
|---|---|
| `RPG_BACKUP_ENVIRONMENT`, `RPG_BACKUP_SERVICE` | monitor와 동일한 영문/숫자/`-`/`_` 1~64자 범위 |
| `RPG_BACKUP_STANZA` | 단일 pgBackRest stanza, 같은 문자/길이 제한 |
| `RPG_BACKUP_CONFIG` | 비공개 pgBackRest 설정의 절대 경로 |
| `RPG_BACKUP_STATE_DIRECTORY` | DB 서비스 계정 소유 비공개 절대 디렉터리. 0700 적용·링크 거절 |
| `RPG_BACKUP_PGBACKREST`, `RPG_BACKUP_PSQL` | 선택 절대 실행 경로, 기본 `/usr/bin/pgbackrest`, `/usr/bin/psql` |
| `PGSERVICE`, `PGSERVICEFILE`, `PGPASSFILE` 등 | psql 전용 읽기 계정/비공개 자격. 명령 인자에 연결 문자열/암호를 넣지 않음 |

pgBackRest 설정은 별도 DB 호스트와 분리된 repo1, `repo1-cipher-type=aes-256-cbc`, 비공개 cipher pass, `repo1-retention-full=2`, 주 full/일 incremental을 사용한다. DB의 `archive_command`는 같은 stanza/config의 `pgbackrest archive-push %p`, `archive_mode=on`, `archive_timeout='5min'`을 사용한다. 실제 repo 주소·접근 자격·암호·서비스 계정은 배포 입력이다. 저장소 데이터를 앱이 직접 압축/복사/삭제하지 않는다.

수집용 PostgreSQL 역할은 CONNECT와 `pg_monitor` 읽기 권한, `pg_control_system()`·`pg_ls_archive_statusdir()` 실행 권한을 사용한다. 해당 두 함수의 명시적 EXECUTE grant와 `default_transaction_read_only=on`을 설정한다. 앱 테이블의 INSERT/UPDATE/DELETE나 superuser는 필요하지 않다. pgBackRest backup 자체의 DB/파일 접근 자격과 분리한다.

## 작업 실행과 주기

같은 비공개 환경에서 다음 고정 명령을 실행한다. 운영 타이머/서비스 등록은 배포에서 수행한다.

```sh
python3 /opt/project-rpg/backup_status.py backup --type full
python3 /opt/project-rpg/backup_status.py backup --type incr
python3 /opt/project-rpg/backup_status.py collect
```

주 1회 full, 매일 incremental, 60초마다 collect를 등록한다. **예약 backup은 이 래퍼를 통해 실행**해야 최근 실패가 즉시 수집된다. pgBackRest info는 성공 이력을 제공하지만 실패한 명령의 종료 결과를 모두 보존하는 작업 ledger가 아니다. 직접 실행한 외부 명령의 과거 실패까지 추론하지 않는다. 최초 수집 전 래퍼로 첫 백업을 수행해 `backup-attempts.json`을 만든다. 첫 full 하나뿐인 상태는 두 체인 보존 경보가 발생하며 서비스 투입 전 두 full 체인을 준비한다.

작업은 실행 전 Running, 종료 후 Success/Failed·시각·attemptId·exitCode를 `backup-attempts.json`에 fsync·원자 교체한다. 같은 attemptId의 시작/종료를 고정 형식으로 서비스 journal에 남기며 수집 실패는 고정 단계명만 출력한다. backup의 native 파일 로그는 pgBackRest 설정의 비공개 log-path/회전 정책을 사용한다. 로그 디렉터리를 서비스 계정에 허용하고 원문은 관리 호스트에 내보내지 않는다. full 실패는 incremental 성공으로 지우지 않으며 같은 종류의 다음 성공으로 갱신한다. backup 프로세스 한도는 1시간이고 실제 DB 크기에 맞는지 배포 리허설에서 확인한다. timeout/취소는 자기 프로세스 그룹만 종료하고 실패를 기록한다. 강제 종료 뒤 Running이 2시간 넘게 남으면 실패로 관측한다. backup/collect는 각각 비공개 flock으로 중복 실행을 거절한다. systemd 서비스는 자기 cgroup의 자식도 회수하는 기본 KillMode를 유지한다.

collect는 pgBackRest `--repo=1 --output=json info`와 읽기 전용 psql 조회를 사용한다. 각 수집 명령은 5초 timeout/출력 1MiB 한도, SQL은 3초 statement_timeout이다. native info의 stanza/current database system-id와 SQL cluster identity를 대조하며 이전 클러스터 백업을 현재 성공으로 세지 않는다. 결과에는 범위·관측시각·성공/실패·full/incr 시각·full 체인 수·archive 설정/성공·실패시각·pending WAL 수/가장 오래된 시각만 포함한다. 파일 경로·DB 식별자·WAL 이름·공지·개인정보·자격·명령 오류 원문은 포함하지 않는다.

정상 수집은 `backup-status.json`을 0600 임시 파일→fsync→같은 디렉터리 원자 rename으로 교체한다. 수집 실패도 `collectionSucceeded=false`를 게시하며 성공 종료로 보고하지 않는다. 게시 자체가 실패하면 이전 파일은 관측시각 기준으로 만료된다. 손상/범위 불일치 작업 ledger는 자동 초기화하지 않는다. 디렉터리 유실 후 옛 작업의 성공/실패를 추측하지 않는다.

## 관리 호스트 전달과 설정

DB 호스트의 `backup-status.json` **한 개만** 관리 호스트의 전용 로컬 파일로 전달한다. 전용 읽기 자격/ACL과 기존 관리망 파일 전달 수단을 사용하고 수신 측도 같은 디렉터리 임시 파일→원자 rename을 적용한다. 부분 파일을 덮어쓰거나 쓰기 자격을 monitor에 부여하지 않는다. 작업 ledger·pgBackRest 설정/암호·원본 DB/repo를 전달하지 않는다. 실제 전달 경로/계정/수단은 배포 입력이며 이번 구현은 원격 계정 생성·외부 전송을 수행하지 않는다.

monitor에 `RPG_MONITOR_BACKUP_STATUS_PATH`를 그 **로컬 절대 파일 경로**로 설정한다. 기본 빈 값은 기존 monitor 동작을 보존한다. 운영 배포에서는 반드시 설정해야 백업 감시가 활성화된다. UNC 경로는 받지 않는다. reader는 계약 v1/환경/서비스/필수 필드·시간/수치 일관성을 검증하며 파일 수정 시각으로 오래된 내용을 정상화하지 않는다. 모니터링을 비활성화해도 저장된 활성 사건을 임의 복구/삭제하지 않는다.

## 경보와 복구

| 사건 키 | 최초 경보 조건 |
|---|---|
| `backup_unavailable` | 파일 없음/권한/손상/범위 불일치/수집 실패, 관측시각 2분 초과 또는 미래 30초 초과 |
| `backup_failed` | repo 상태/암호화 비정상 또는 full/incr 작업 실패 |
| `backup_overdue` | full 성공 없음/8일 초과 또는 daily(full·incr 중 최신) 성공 없음/26시간 초과 |
| `backup_retention` | 성공한 full 메타데이터 체인 2개 미만 |
| `backup_archive` | archive mode/timeout 설정 부적합, 마지막 실패가 성공보다 최신, 또는 pending WAL의 가장 오래된 항목 2분 초과 |

알게 된 실패는 첫 표본에 경보한다. Unknown은 다른 백업 사건을 복구하는 정상 표본으로 쓰지 않는다. backend 종료와 백업 상태 전달 실패는 각각 독립 사건이다. archive 성공 시각이 오래됐더라도 실패/대기 WAL이 없으면 idle DB로 취급한다. WAL 전송 순서는 보장되지 않으므로 최근 archive 성공 하나가 오래된 pending WAL을 숨기지 못한다. 기본 60초 수집+30초 monitor 주기에서 WAL 오류/적체를 5분 안에 발견하는 정책이며 실제 운영 지연 목표 달성은 별도 리허설 대상이다.

full 체인 수는 성공 메타데이터 수이며 원격 모든 객체의 무결성·실제 복원 가능성을 증명하지 않는다. 백업 생성, 보존, WAL 검증, 실제 복원은 pgBackRest와 복구 리허설의 책임이다. RPO15분/RTO4시간을 이미 달성했다고 표시하지 않는다.

## 검증

`Scripts/Tests/Run-OperationsBackupTests.ps1 -FullSuite`는 게시본+본인 명시 입력만 별도 빌드한다. Docker의 자기 임시 PostgreSQL 17에 pgBackRest/Python을 설치하고 실제 암호화 repo의 full2/incr1, 읽기 전용 역할 수집, 실패한 job receipt·연결 실패 Unknown을 검사한다. Python 단위 검사와 실제 JSON→.NET reader→정책→영속 HTTPS 재전송/복구·기존 운영툴 Memory 회귀를 실행한다. 자기 컨테이너/DB/repo/Temp는 finally 폐기하고 작은 결과/입력 해시만 남긴다.

실제 외부 repo·관리 호스트 전달·운영 타이머·복원/PITR·운영 크기 성능은 검증 범위 밖이다. 기존 전체 PostgreSQL 운영툴 회귀의 migration 개수 기대값 불일치는 [장기 Drain 완료 기록](Online_RPG_Development_Roadmap.md#12-현재-현황과-다음-작업-체크리스트-2026-10-20)에 별도 기록되어 있다.

근거: [pgBackRest info/backup 공식 계약](https://pgbackrest.org/command.html), [pgBackRest 운영·복원 지침](https://pgbackrest.org/user-guide.html), [PostgreSQL archive 상태/파일 조회](https://www.postgresql.org/docs/17/functions-admin.html), [pg_stat_archiver](https://www.postgresql.org/docs/17/monitoring-stats.html#MONITORING-PG-STAT-ARCHIVER-VIEW). native JSON 필드는 [pgBackRest 자체 info 회귀 검사](https://github.com/pgbackrest/pgbackrest/blob/main/test/src/module/command/infoTest.c)와 실제 실행 출력으로 대조한다.
