<!DOCTYPE html>
<html>
<head>
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
<div>
<?php
$target_dir = "/tmp/";
$target_file = $target_dir . basename($_FILES["fileToUpload"]["name"]);
$uploadOk = 1;
$fileType = strtolower(pathinfo($target_file,PATHINFO_EXTENSION));

// Check if file already exists
//if (file_exists($target_file)) {
//    echo "Sorry, file already exists.";
//    $uploadOk = 0;
//}

// Check file size
if ($_FILES["fileToUpload"]["size"] > 500000) {
    echo "Sorry, your file is too large.";
    $uploadOk = 0;
}

/* Allow tar and tgz file formats */
if($fileType != "tgz" && $fileType != "tar") {
    echo "Sorry, only tar gzip (tgz) files are allowed.";
    $uploadOk = 0;
}

// Check if $uploadOk is set to 0 by an error
if ($uploadOk == 0) {
    echo "Sorry, your file was not uploaded.";
// if everything is ok, try to upload file
} else {
    if (move_uploaded_file($_FILES["fileToUpload"]["tmp_name"], $target_file)) {
        echo "The file ". basename( $_FILES["fileToUpload"]["name"]). " has been uploaded.";
    } else {
        echo "Sorry, there was an error uploading your file.";
    }
}
?>
</div>
</body>
</html>
