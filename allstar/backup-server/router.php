<?php

if (php_sapi_name() == "cli-server") {

   // Default index file
   define("DIRECTORY_INDEX", "index.html");

   $ini_array = parse_ini_file("allowedHosts.ini");
   $IPs = explode(',', $ini_array['allowed']);
   $allowed = array();
   $cidrs = array();
   foreach ($IPs as $ipaddr) {
      if (!strpos($ipaddr, '/') == FALSE) {
         $cidrs[] = $ipaddr;
      } else {
         $allowed[] = $ipaddr;
      }
   }
   #print_r($allowed);
   #return true;

   $extensions = array("gzip", "html", "php", "ico", "css", "png");

   $config["hostsAllowed"] = array();

   $path = parse_url($_SERVER["REQUEST_URI"], PHP_URL_PATH);
   $ext = pathinfo($path, PATHINFO_EXTENSION);
   if (empty($ext)) {
      $path = rtrim($path, "/") . "/" . DIRECTORY_INDEX;
      $ext = pathinfo($path, PATHINFO_EXTENSION);
   }

   if (in_array($ext, $extensions)) {
      $ip=$_SERVER['REMOTE_ADDR'];
      $b = $ip == '127.0.0.1';
      $c = in_array($ip, $config['hostsAllowed']);
      $d = in_array($ip, $allowed);
      if (count($cidrs) > 0) {
         $e = false;
         foreach($cidrs as $cidr) {
            $e = cidr_match($ip, $cidr);
         }
      } else {
         $e = false;
      }

      if ($b or $c or $d or $e) {
            // let the server handle the request as-is
            return false;
      }
   }
}

function cidr_match($ip, $range) {
    list ($subnet, $bits) = explode('/', $range);
    $ip = ip2long($ip);
    $subnet = ip2long($subnet);
    $mask = -1 << (32 - $bits);
    $subnet &= $mask; # nb: in case the supplied subnet wasn't correctly aligned
    return ($ip & $mask) == $subnet;
}

http_response_code(403);

