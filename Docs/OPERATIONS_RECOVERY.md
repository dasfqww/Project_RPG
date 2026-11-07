# PostgreSQL 복구 리허설과 운영 전환 절차

2026-11-08. [restore_rehearsal.py](../Scripts/Operations/restore_rehearsal.py)는 읽기 전용 체크포인트 수집과 **일회용 별도 인스턴스**의 full/incremental/PITR 복구를 제공한다. 원본을 덮어쓰거나 앱 연결을 전환하지 않는다. 정상·실패 모두 자기 복원본을 정지/폐기하며 운영 웹에는 복원 버튼을 추가하지 않는다. [백업 수집](OPERATIONS_BACKUP.md)과 같은 pgBackRest/Python 실행 경계를 사용한다.

## 구성과 검증 경계

`Settings`는 범위·보호 경로·시간 한도를 검증한다. `DatabaseVerifier`는 고정 SELECT와 READ ONLY/REPEATABLE READ로 public 테이블을 검사한다. `RecoveryRehearsal`은 새 작업공간 생성·pgBackRest 복원·사설 PostgreSQL 시작·목표 도달·검증·정리 순서를 담당한다. 명령 실행/유계 진단/원자 파일 기록은 기존 백업 어댑터를 재사용한다.

지원 대상은 Linux PostgreSQL17, 원본과 동일한 정확한 서버 버전, 기본 테이블스페이스다. 체크포인트는 사용자 테이블스페이스가 있으면 거절한다. 백업부터 복구 시점 사이에도 테이블스페이스 생성/삭제가 없는 프로젝트 프로필이어야 한다. 앱의 모든 public 테이블과 `rpg_schema_migrations` 이력을 대조한다. 다른 DB·다른 schema·실제 게임/API 업무 재실행까지 검증한 결과로 확대하지 않는다.

## 배포 입력

`restore_rehearsal.py`와 `backup_status.py`를 같은 디렉터리에 배포한다. Python3.9 이상, pgBackRest, PostgreSQL17의 psql/pg_ctl을 사용한다. PostgreSQL을 실행할 수 있는 비특권 OS 서비스 계정으로 실행한다.

| 환경 변수 | 계약 |
|---|---|
| `RPG_RESTORE_ENVIRONMENT`, `RPG_RESTORE_SERVICE`, `RPG_RESTORE_STANZA` | 영문/숫자/`-`/`_` 1~64자. 체크포인트와 같아야 함 |
| `RPG_RESTORE_DATABASE`, `RPG_RESTORE_USER` | 동일 문자 제한의 DB/검사 역할. 사용자 입력 연결 문자열을 받지 않음 |
| `RPG_RESTORE_SOURCE_DATA_DIRECTORY` | 원본 PGDATA의 canonical 절대 경로. 보호 경로/원본 신원 대조용 |
| `RPG_RESTORE_SOURCE_SOCKET_DIRECTORY`, `RPG_RESTORE_SOURCE_PORT` | capture 전용 로컬 Unix socket. 포트 기본5432. `/var/run` 같은 읽기 전용 alias는 canonical 경로로 해석 |
| `RPG_RESTORE_SOURCE_PASSFILE` | 선택 비공개 절대 비밀번호 파일. 원본 capture에만 사용하고 복원본에 전달하지 않음 |
| `RPG_RESTORE_CONFIG` | 비공개 pgBackRest 설정 절대 경로. repo1/stanza/복호화 자격 포함. 운영에서는 저장소 읽기 전용 자격 사용 |
| `RPG_RESTORE_SCRATCH_DIRECTORY` | 새 복원본을 만드는 비공개 절대 경로. 원본의 내부/상위 경로·링크·루트 `/` 거절 |
| `RPG_RESTORE_PROOF_DIRECTORY` | 별도 비공개 결과 디렉터리. scratch·원본과 분리, 0700 적용 |
| `RPG_RESTORE_PG_BIN_DIRECTORY` | PostgreSQL17 bin 절대 경로 |
| `RPG_RESTORE_PGBACKREST`, `RPG_RESTORE_PSQL` | 선택 절대 실행 경로. 기본 `/usr/bin/pgbackrest`, `/usr/bin/psql` |
| `RPG_RESTORE_TIMEOUT_SECONDS` | 복구 예산, 기본14400초·범위30~14400. 초과 결과를 성공으로 표시하지 않음 |
| `RPG_RESTORE_QUERY_TIMEOUT_SECONDS` | 지문 조회 한도, 기본120초·범위10~3600. 실제 DB 크기의 정렬/임시 공간/조회 시간을 리허설에서 조정 |

검사 DB 역할에는 public의 모든 앱 테이블 SELECT, `pg_monitor`, `pg_control_system()`·`pg_get_wal_replay_pause_state()` EXECUTE가 필요하다. 이 역할을 백업 전에 만들고 앱 테이블 변경 후 SELECT 권한을 갱신한다. 일반 앱 쓰기/복원 수행 권한과 분리한다. 원본 조회는 비밀번호 프롬프트 없이 실행하며 PG 서비스/hostaddr/options를 상속하지 않는다. 비공개 passfile이 필요한 배포는 위 전용 입력을 사용한다.

## 체크포인트 수집

점검 Drain을 설정하고 외부 수단으로 생산자·worker·앱 쓰기를 중지한 뒤, 원본 또는 같은 schema/data 상태의 리허설 DB에서 실행한다. **Drain만으로 쓰기 중지가 증명되지 않는다.** `--source-quiesced`는 작업자의 외부 쓰기 중지 확인을 기록하는 입력이며 도구가 이를 자동 증명한다는 뜻이 아니다.

```sh
python3 /opt/project-rpg/restore_rehearsal.py capture \
  --source-quiesced --checkpoint "$RPG_RESTORE_PROOF_DIRECTORY/checkpoint.json"
```

체크포인트는 새 파일만 허용한다. capture flock으로 같은 경로의 경합을 막고 0600 임시 파일/fsync/원자 교체로 기록한다. 환경·서비스·stanza·cluster 신원·PG 버전·DB 관측시각·동일 migration 버전/이름/SHA와 각 public 테이블의 행 수/SHA256을 보관한다. 계정·아이템/우편·재화·영수증·Processing 정산·인증 데이터의 행 내용은 파일에 저장하지 않는다.

읽기 트랜잭션 하나에서 각 테이블을 고정 순서로 순회한다. 행 스트리밍으로 SHA256을 계산하므로 전체 테이블을 메모리나 proof 파일에 모으지 않는다. 원본/복원본의 JSON 출력·정렬·UTC 기준을 동일하게 유지한다. 한 행1MiB/테이블256개/체크포인트64KiB 한도를 초과하면 실패한다. 정렬 work_mem은16MiB이고 큰 데이터는 DB 임시 정렬 공간을 사용할 수 있으므로 운영 크기의 읽기 부하/공간도 별도로 측정한다.

## 격리 복구 실행

```sh
# 지정 full 또는 incremental 백업의 consistency 시점. latest 자동 선택 없음.
python3 /opt/project-rpg/restore_rehearsal.py run \
  --checkpoint "$RPG_RESTORE_PROOF_DIRECTORY/checkpoint.json" \
  --backup-set "$BACKUP_SET" --mode immediate

# 체크포인트의 DB 관측시각까지 inclusive PITR.
python3 /opt/project-rpg/restore_rehearsal.py run \
  --checkpoint "$RPG_RESTORE_PROOF_DIRECTORY/checkpoint.json" \
  --backup-set "$BACKUP_SET" --mode time
```

immediate 검사용 체크포인트는 해당 백업과 논리 데이터가 같은 쓰기 중지 구간에 수집한다. time은 JSON의 UTC 시각을 PostgreSQL 복구 설정용 시각으로 변환하고 `target-action=pause`·inclusive를 고정한다. 목표가 백업 이후 유효한 WAL 구간에 있어야 한다. 잘못된 키/없는 WAL/도달할 수 없는 목표는 성공으로 처리하지 않는다.

실행마다 비공개 새 디렉터리를 생성하고 기존 PGDATA·delta/force 복원·임의 명령/SQL 입력을 노출하지 않는다. native 환경의 PG/PGBACKREST 옵션을 상속하지 않으며 link map을 초기화하고 테이블스페이스를 자기 작업공간으로 매핑한다. 복원 후 외부 링크가 남으면 시작하지 않는다. 원본 경로에 대한 pg_ctl stop을 호출하지 않는다.

복원본은 TCP listen·원본 streaming 연결·archive 전송·외부 PID 파일을 끄고 소유자 전용 Unix socket으로만 실행한다. 복사된 운영 HBA 대신 **자기 복원본에만** 검사 역할용 local 규칙을 쓴다. 원본 비밀번호/PG service/hostaddr는 복원본 검사에 사용하지 않는다. `pg_is_in_recovery()`와 실제 replay pause 상태를 확인한 뒤, cluster/서버 버전·migration 이력·전체 public 데이터 지문이 체크포인트와 같은지 대조한다. DB가 연결을 받기 시작했다는 이유만으로 복구 성공을 표시하지 않는다.

## 결과와 정리

실행 UUID(`attemptId`), 단계, native 종료 코드, 지정 백업/목표시각, 체크포인트 SHA, schema/data SHA, 경과 시간·데이터셋 한정4시간 목표 여부를 기록한다. `productionReady`는 false다. 단계 로그에는 SQL/행/자격 원문을 출력하지 않는다. 실패 시 stderr는 최대64KiB로 수집하고 필요한 비공개 꼬리 로그만 파일당16KiB까지 보관한다. 이 로그를 webhook·제품 저장소·일반 로그 수집기에 복사하지 않는다.

정상/실패/취소 모두 자기 PID/PGDATA·소유 표식을 대조해 복원본을 fast stop하고 자기 작업공간만 정리한다. symlink는 대상을 따라 삭제하지 않는다. 정지/소유 확인 실패 시 삭제를 강행하지 않고 `cleanupComplete=false`, 남은 경로를 결과에 기록한다. 이 경우 같은 서비스 계정이 표식/프로세스를 대조해 인계 정리한다. 결과/체크포인트·작은 실패 로그와 DB 사본을 구분하며 대형 사본을 다음 작업까지 보존하지 않는다.

작은 합성 데이터의 실행 시간이 짧다는 사실은 운영 DB의 RPO15분/RTO4시간 달성 증거가 아니다. 운영 용량·원격 repo·전달 지연·실제 장애 시각/선택 시점·키/앱 재시작/연결 전환까지 포함해 최초 서비스 전, 중요 schema/backup 설정 변경 후, 이후 월1회 측정한다.

## 실제 운영 복구와 연결 전환

다음은 리허설 도구가 자동 실행하지 않는 운영 절차다. 리허설은 항상 복원본을 폐기하므로 실제 서비스 목적지는 별도로 준비한다.

1. Drain → 생산자/worker/앱 쓰기 중지·차단 확인 → 장애 원본과 사건 기록을 읽기 전용으로 보존한다. 실패/미확정 정산을 시간 경과만으로 지우지 않는다.
2. 별도 목적지에 필요한 backup/WAL을 복원하고, 목표 pause·동일 release의 migration 이력/SHA·계정/아이템/우편/재화/영수증/정산을 대조한다. schema 불일치를 임의 자동 migration으로 덮지 않는다.
3. 목적지는 관리망에만 격리한 채 활성화 준비를 한다. 실제 목적지 승격 후 앱/worker를 시작하기 전에 운영 세션·플레이어 인증·미사용 입장 티켓·서버 자격을 폐기한다. 현재 저장 경계는 `ops_operator_sessions.revoked_at`, `auth_sessions`, `game_join_tickets`, `game_server_credentials`이며 불변 `game_join_ticket_consumptions`/취소·보상 영수증은 삭제 대상이 아니다.
4. 배포 설정의 `Operations:AuthEpoch`와 `Auth:AdminToken`을 새 값으로 교체하고 옛 앱/게임 서버 프로세스를 재사용하지 않는다. 운영 cookie와 옛 bearer를 거절하고 새 Steam 인증/서버 등록만 허용하는지 확인한다. 실제 값은 비공개 배포 입력으로 유지한다.
5. 복원된 리스·Processing 작업은 기존 소유/만료/영수증 규칙으로 대조한다. 미확정 요청은 원래 requestId로 조사하며, 복구 시점 이후 원본에만 남은 지급을 대조 전 자동 재전송하지 않는다.
6. 격리된 새 앱에서 `/health/ready`·schema/인증/계약 확인 → 관리 수단으로 연결 전환 → 새 인증/입장/정산 정상 확인 → Open 순서로 진행한다. 원본 사본은 대조·보존 정책의 종료 조건을 확인한 뒤 정리한다.

## 검증 경로

`Scripts/Tests/Run-OperationsRecoveryTests.ps1`은 게시본 Database SQL과 명시한 스크립트만 고정한다. 자기 PostgreSQL17 컨테이너에서 실제 공개 schema/migration을 적용하고 계정·아이템/우편·큰 정수 재화·영수증·Processing·옛 인증 행을 합성한다. 암호화 full/incr 이후 PITR 경계 전후의 별도 커밋을 만들고, 정상3종(full/incr/time)·데이터 불일치/잘못된 키/목표 미도달/누락 WAL 실패4종을 검사한다. 원본 public 데이터와 읽기 전용 repo 바이트가 바뀌지 않았는지도 확인한다.

Python 단위 검사, 기존 백업 어댑터 회귀, 실제 복원·별도 pause 인스턴스 검사와 자기 컨테이너/Temp 정리를 포함한다. 실제 게임/Steam/API 업무 재실행·원격 운영 repo·운영 용량·인증 폐기/연결 전환은 이 검사의 범위 밖이다.

근거: [pgBackRest restore/target/link/table mapping](https://pgbackrest.org/command.html#command-restore), [PostgreSQL17 recovery target/action](https://www.postgresql.org/docs/17/runtime-config-wal.html#RUNTIME-CONFIG-WAL-RECOVERY-TARGET), [PostgreSQL17 recovery 상태 조회](https://www.postgresql.org/docs/17/functions-admin.html#FUNCTIONS-RECOVERY-CONTROL).
