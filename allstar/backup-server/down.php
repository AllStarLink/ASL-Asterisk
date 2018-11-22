<!DOCTYPE html>
<html>
<title>Backup My AllStarLink Node</title>
<link type="text/css" rel="stylesheet" href="allmon.css">
</head>

<body>
<div id="header">
<div id="headerTitle"><a href="/">Backup My Node</a></div>
<div id="headerTag">
Local backup for my Node 
</div>
<div id="headerImg"><img src="allstarLogo.png" alt="Allstar Logo"></div>
</div>

<div id="menu">
<ul>
<li><a href="up.html">Upload Backup to Node</a></li>
<li><a href="down.php">Download Backup from Node</a></li>
</ul>
</div>

<div class="clearer"></div>
<h2>Backup Download</h2>
<?php
$dir = "/tmp/";

$files = array();
$allFiles = scandir($dir);
foreach ($allFiles as $value) {
    if (preg_match('/\.tgz$/', $value)) {
       $files[] = $value;
    }
}

if (count($files) > 0 ) {
    echo '<p>Click the file to begin downloading from node.</p>';
    foreach($files as $file){
        $url = $dir . $file;
        print "<a href=\"download.php?file=$url\">$file</a><br/>"; 
         #echo '<a href=" . $url . 'download.php?file=' . $file . '">' . $file . '</a><br>';
    }
} else {
    echo '<p>No backup files found.</p>';
}

?>
</body>
</html>