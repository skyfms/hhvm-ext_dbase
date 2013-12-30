<?php
// database "definition"
$def = array(
  array("date",     "D"),
  array("name",     "C",  50),
  array("age",      "N",   3, 0),
  array("email",    "C", 128),
  array("ismember", "L")
);

// creation
$db = dbase_create('/tmp/test.dbf', $def);
if (!$db) {
  die("Error, can't create the database\n");
}

$column_info = dbase_get_header_info($db);
print_r($column_info); echo "\n";

if ($db) {
  dbase_add_record($db, array(
    date('Ymd'),
    'Maxim Topolov',
    '23',
    'max@example.com',
    'T'
  ));
  dbase_close($db);
}

$db = dbase_open('/tmp/test.dbf', 2);
if ($db) {
  $numfields = dbase_numfields($db);
  echo 'DB has ' . $numfields . " fields\n";
  $record_numbers = dbase_numrecords($db);
  echo 'DB has ' . $record_numbers . " records\n";
  for ($i = 1; $i <= $record_numbers; $i++) {
    $row = dbase_get_record_with_names($db, $i);
    print_r($row);
    echo "\n";
  }
  echo "Modifying first record\n";
  $row = dbase_get_record_with_names($db, 1);
  $row['name'] = 'Andris Berzins';
  unset($row['deleted']);
  echo "Going to replace with:\n"; print_r($row); echo "\n";
  dbase_replace_record($db, $row, 1);
  echo "Record after modifying (not assoc):\n";
  $row = dbase_get_record($db, 1);
  print_r($row); echo "\n";
  echo "Now deleting\n";
  dbase_delete_record($db, 1);
  $record_numbers = dbase_numrecords($db);
  echo 'Now DB has ' . $record_numbers . " records\n";
  for ($i = 1; $i <= $record_numbers; $i++) {
    $row = dbase_get_record_with_names($db, $i);
    print_r($row);
    echo "\n";
  }
  echo "Packing\n";
  dbase_pack($db);
  $record_numbers = dbase_numrecords($db);
  echo 'Now DB has ' . $record_numbers . " records\n";
  for ($i = 1; $i <= $record_numbers; $i++) {
    $row = dbase_get_record_with_names($db, $i);
    print_r($row);
    echo "\n";
  }
  dbase_close($db);
}

