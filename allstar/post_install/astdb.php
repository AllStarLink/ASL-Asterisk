#! /usr/bin/php -q
<?php

// Read AllStar database
$url = "https://allstarlink.org/cgi-bin/allmondb.pl";
$astArr = file($url);

// Read private nodes
$privatefile = "privatenodes.txt";
$privateArr = array();
if (file_exists($privatefile)) {
    $privateArr = file($privatefile);
}

// Merge with private nodes
if (!empty($privateArr)) {
    $fileArr = array_merge($astArr, $privateArr);
} else {
    $fileArr = $astArr;
}

// Sort
natsort($fileArr);

// Output
$db = "astdb.txt";
if (! $fh = fopen($db, 'w')) {
    die("Cannot open $db.");
}
if (!flock($fh, LOCK_EX))  {
    echo 'Unable to obtain lock.';
    exit(-1); 
}
foreach($fileArr as $line) {
    if (strlen(trim($line)) == 0) {
        continue;
    }
    fwrite($fh, $line);
}
fclose($fh);
?>
