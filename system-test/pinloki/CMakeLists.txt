# Basic test
add_test_executable_ex(NAME pinloki_basic SOURCE pinloki_basic.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Checks that binlog files are created and indexed correctly
add_test_executable_ex(NAME pinloki_files SOURCE pinloki_files.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# PURGE BINARY LOGS test
add_test_executable_ex(NAME pinloki_purge SOURCE pinloki_purge.cc
  CONFIG pinloki_purge.cnf VMS repl_backend LABELS pinloki)

# GTID position test
add_test_executable_ex(NAME pinloki_gtid SOURCE pinloki_gtid.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Restart MaxScale during replication
add_test_executable_ex(NAME pinloki_restart SOURCE pinloki_restart.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Do STOP SLAVE; START SLAVE; while new events are being generated
add_test_executable_ex(NAME pinloki_start_stop SOURCE pinloki_start_stop.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Promote a slave to a master and demote the master to a slave
add_test_executable_ex(NAME pinloki_change_master SOURCE pinloki_change_master.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Replicate large events (larger than 16MB)
add_test_executable_ex(NAME pinloki_large_event SOURCE pinloki_large_event.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Test automatic master selection
add_test_executable_ex(NAME pinloki_select_master SOURCE pinloki_select_master.cc
  CONFIG pinloki_select_master.cnf VMS repl_backend LABELS pinloki)

# TLS for master replication
add_test_executable_ex(NAME pinloki_tls SOURCE pinloki_tls.cc
  CONFIG pinloki.cnf VMS repl_backend backend_ssl LABELS pinloki)

# Test switchover
add_test_executable_ex(NAME pinloki_switchover SOURCE pinloki_switchover.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# Test a likely upgrade scenario
add_test_executable_ex(NAME pinloki_upgrade SOURCE pinloki_upgrade.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# MXS-3674: Pinloki hangs if "maxctrl show threads" and STOP SLAVE are executed at the same time
add_test_executable_ex(NAME pinloki_hang SOURCE pinloki_hang.cc
  CONFIG pinloki.cnf VMS repl_backend LABELS pinloki)

# MXS-4103: Pinloki doesn't decrypt passwords with select_master=true
add_test_executable_ex(NAME pinloki_passwords SOURCE pinloki_passwords.cc
  CONFIG pinloki_passwords.cnf VMS repl_backend LABELS pinloki)

# Test pinloki with galera
add_test_executable_ex(NAME pinloki_galera SOURCE pinloki_galera.cc
  CONFIG pinloki_galera.cnf VMS repl_backend galera_backend LABELS pinloki)

# MXS-2662: Pinloki encryption
add_test_executable_ex(NAME pinloki_encryption_file SOURCE pinloki_encryption_file.cc
  CONFIG pinloki_encryption_file.cnf VMS repl_backend LABELS pinloki)

add_test_executable_ex(NAME pinloki_encryption_vault SOURCE pinloki_encryption_vault.cc
  CONFIG pinloki_encryption_vault.cnf VMS repl_backend LABELS pinloki)

add_test_executable_ex(NAME pinloki_encryption_kmip SOURCE pinloki_encryption_kmip.cc
  CONFIG pinloki_encryption_kmip.cnf VMS repl_backend LABELS pinloki)

# MXS-3004: Semi-synchronous replication
add_test_executable_ex(NAME pinloki_semisync SOURCE pinloki_semisync.cc
  CONFIG pinloki_semisync.cnf VMS repl_backend LABELS pinloki)

add_test_executable_ex(NAME pinloki_ddl_only SOURCE pinloki_ddl_only.cc
  CONFIG pinloki_ddl_only.cnf VMS repl_backend LABELS pinloki)
