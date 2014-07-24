<?hh

<<__Native>> function dbase_open (string $filename, int $mode): mixed;
<<__Native>> function dbase_create (string $filename, mixed $fields): mixed;
<<__Native>> function dbase_close (int $dbase_identifier): bool;
<<__Native>> function dbase_numrecords (int $dbase_identifier): mixed;
<<__Native>> function dbase_numfields (int $dbase_identifier): mixed;
<<__Native>> function dbase_add_record (int $dbase_identifier, mixed $record): bool;
<<__Native>> function dbase_get_record (int $dbase_identifier, int $record_number): mixed;
<<__Native>> function dbase_delete_record (int $dbase_identifier, int $record_number): bool;
<<__Native>> function dbase_replace_record (int $dbase_identifier, mixed $record, int $record_number): bool;
<<__Native>> function dbase_pack (int $dbase_identifier): bool;
<<__Native>> function dbase_get_record_with_names (int $dbase_identifier, int $record_number): mixed;
<<__Native>> function dbase_get_header_info (int $dbase_identifier): mixed;

