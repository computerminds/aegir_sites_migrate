<VirtualHost *:<?php print $http_port; ?>>
  ServerName <?php print $this->data['uri']; ?>

  <?php
  if (sizeof($this->aliases)) {
    print "\n ServerAlias " . implode("\n ServerAlias ", $this->aliases) . "\n";
  }

  print " RewriteEngine on\n";
  foreach ($this->aliases as $alias) {
    print " RewriteCond %{HTTP_HOST} ^{$alias}$ [NC]\n";
    print " RewriteRule ^/*(.*)$ http://{$this->data['uri']}/$1 [L,R=301]\n";
  }

  ?>

  ProxyRequests Off
  <Proxy *>
    Order deny,allow
    Allow from all
  </Proxy>

  ProxyPass / http://<?php print $http_proxy_forward; ?>/
  ProxyPassReverse / http://<?php print $http_proxy_forward; ?>/
  ProxyPreserveHost On

</VirtualHost>
