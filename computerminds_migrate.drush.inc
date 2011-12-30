<?php

define('COMPUTERMINDS_OLD_SERVER_IP', '192.168.1.2');
define('COMPUTERMINDS_OLD_HOSTMASTER_NAME', 'old_hostmaster');
define('COMPUTERMINDS_PLATFORM_NAME', 'computerminds-platform-1.0');
define('COMPUTERMINDS_NEW_DB_SERVER', 'server_localhost');

// Even though we don't extend it, we do make use of it's config classes.
include_once(_computerminds_migrate_get_provision_path() . '/provision.config.inc');

/**
 * Adjust this to return the path to provision on your machine.
 */
function _computerminds_migrate_get_provision_path() {
  return '/var/aegir/.drush/provision';
}

/**
 * Implements hook_drush_command().
 */
 function computerminds_migrate_drush_command() {
  $items = array();

  $items['platform-list-sites'] = array(
    'description' => "List the sites on a given platform",
    'arguments' => array(
      'platform' => 'The platform to get the sites of.',
    ),
    'drupal dependencies' => array('hosting'),
  );

  $items['pre-migrate-setup'] = array(
    'description' => "Sets up the current hostmaster ready to migrate",
    'drupal dependencies' => array('hosting'),
  );

  $items['migrate-computerminds-one'] = array(
    'description' => 'migrate a single computerminds site from the old server to the new one.',
    'drupal dependencies' => array('hosting'),
    'arguments' => array(
      'site' => 'The URI of the site to migrate.',
    ),
  );

  $items['migrate-computerminds-all'] = array(
    'description' => 'migrate a single computerminds site from the old server to the new one.',
    'drupal dependencies' => array('hosting'),
  );

   return $items;
 }

/**
 * Lists all sites from a particular platform.
 *
 * Just returns an array of URLs.
 */
function drush_computerminds_migrate_platform_list_sites($platform) {
  $sites = array();

  $platform_node = hosting_context_load($platform);

  // Load the platform:
  $all_sites = hosting_get_sites_by_status($platform_node->nid, HOSTING_SITE_ENABLED);

  foreach ($all_sites as $site) {
    $sites[] = $site->title;
  }
  
  // This is a bit of a fragile way to return the data, but I couldn't seem to
  // get Drush to pass the structured data back properly, so we'll do this,
  // which works.
  drush_print_r(serialize($sites));
}

/**
 * Sets up the current hostmaster ready for migration.
 */
function drush_computerminds_migrate_pre_migrate_setup() {
  $platform = COMPUTERMINDS_PLATFORM_NAME;
  $old_hostmaster = '@' . COMPUTERMINDS_OLD_HOSTMASTER_NAME;
  $platform_context = d('@platform_' . $platform);
  $web_server = d($platform_context->web_server);

  $old_sites = computerminds_migrate_get_all_sites($old_hostmaster, $platform);

  foreach ($old_sites as $site) {
    // Need to ensure that we have a mod_proxy vhost for this site.
    drush_log('Creating a mod proxy vhost for: ' . $site);
    $vhost = new provisionConfig_computerminds_proxy($web_server, array('uri' => $site));
    $vhost->write();
  }

  // Now restart the web server.
  $web_server->service('http')->restart();
}

/**
 * Base class for proxied virtual host configuration files.
 */
class provisionConfig_computerminds_proxy extends provisionConfig {
  public $template = 'computerminds_proxy_vhost.tpl.php';
  public $description = 'mod proxy virtual host configuration file';


  function filename() {
    return $this->http_vhostd_path . '/' . $this->data['uri'];
  }

  function process() {
    parent::process();

    $this->data['http_port'] = $this->http_port;
    $this->data['http_proxy_forward'] = COMPUTERMINDS_OLD_SERVER_IP;

    if ($this->aliases && !is_array($this->aliases)) {
      $this->aliases = explode(",", $this->aliases);
    }
    else {
      $this->aliases = array();
    }

    $this->aliases = array_filter($this->aliases, 'trim');

    $uri = $this->data['uri'];
    if (strpos($uri, 'www.') === 0) {
      $this->aliases[] = substr($uri, 4);
    }
    else {
      $this->aliases[] = 'www.' . $uri;
    }
  }

}

/**
 * Implements drush_hook_pre_migrate_computerminds_one().
 *
 * We use the pre command hook to remove our temporary vhost.
 */
function drush_computerminds_migrate_pre_migrate_computerminds_one($site) {
  // Remove the proxy vhost
  $platform = COMPUTERMINDS_PLATFORM_NAME;
  $platform_context = d('@platform_' . $platform);
  $web_server = d($platform_context->web_server);
  drush_log('REMOVING mod proxy vhost for: ' . $site);
  $vhost = new provisionConfig_computerminds_proxy($web_server, array('uri' => $site));
  $vhost->unlink();
  $web_server->service('http')->restart();
}

/**
 * Implements drush_hook_pre_migrate_computerminds_one_rollback().
 *
 * We use the pre rollback command hook to replace our temporary vhost if
 * something when wrong with this migrate.
 */
function drush_computerminds_migrate_pre_migrate_computerminds_one_rollback($site) {
  // Add the proxy vhost.
  $platform = COMPUTERMINDS_PLATFORM_NAME;
  $platform_context = d('@platform_' . $platform);
  $web_server = d($platform_context->web_server);
  drush_log('Creating a mod proxy vhost for: ' . $site);
  $vhost = new provisionConfig_computerminds_proxy($web_server, array('uri' => $site));
  $vhost->write();
  $web_server->service('http')->restart();
}

/**
 * Drush command to migrate a single site from the old server to the new one.
 */
function drush_computerminds_migrate_migrate_computerminds_one($site) {
  // Do a backup on the old server.
  drush_log(dt('Backing up old site: @uri', array('@uri' => $site)), 'ok');
  $suggested = d()->platform->server->backup_path . '/' . $site . '-migrate-' . date("Ymd.His", mktime()) . '.tar.gz';
  drush_backend_invoke_args('@' . ltrim($site, '@') . ' ' . 'provision-backup', array($suggested), array('uri' => $site, 'root' => d(COMPUTERMINDS_PLATFORM_NAME)->root), 'GET', TRUE, NULL, COMPUTERMINDS_OLD_SERVER_IP, 'aegir');

  // Rsync to this machine.
  drush_log('Copying backup from remote server...', 'ok');
  if (drush_core_call_rsync(escapeshellarg('aegir@' . COMPUTERMINDS_OLD_SERVER_IP . ':' . $suggested), escapeshellarg($suggested), array(), TRUE, FALSE)) {
  }
  else {
    return drush_set_error('RSYNC_FAILED', 'Failed to copy the backup from the remote server.');
  }

  // Copy the Aegir context file over.
  $alias_file = '/var/aegir/.drush/' . $site . '.alias.drushrc.php';
  if (drush_core_call_rsync(escapeshellarg('aegir@' . COMPUTERMINDS_OLD_SERVER_IP . ':' . $alias_file), escapeshellarg($alias_file), array(), TRUE, FALSE)) {
    // Now set the new DB server
    $args = array(
      'uri' => "$site",
      "@$site",
      'db_server' => '@' . COMPUTERMINDS_NEW_DB_SERVER,
      'root' => d('platform_ ' . COMPUTERMINDS_PLATFORM_NAME)->root,
      'platform' => '@platform_' . d(COMPUTERMINDS_PLATFORM_NAME)->name,
    );
    drush_backend_invoke('provision-save', $args);
  }
  else {
    return drush_set_error('RSYNC_FAILED', 'Failed to copy the alias from the remote server.');
  }

  drush_log('Copied all files from remote server.', 'ok');

  // Deploy the site.
  provision_backend_invoke($site, 'provision-deploy', array($suggested), array('old_uri' => $site));
  drush_log('Deployed the files and database locally.', 'ok');

  // Import into the frontend, if there are no errors.
  if (!drush_get_error()) {
    drush_log('Importing the site into the frontend...', 'ok');
    provision_backend_invoke('@hostmaster', 'hosting-import', array("@" . $site));
    provision_backend_invoke('@hostmaster', 'hosting-task', array("@" . $site, 'verify'));
    provision_backend_invoke('@hostmaster', 'hosting-task', array("@" . $site, 'enable'));
    drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_LOGIN);

    // Hosting will create a dummy install task, but it'll fail. So we remove it here.
    $ref = hosting_context_load("@" . $site);
    if ($ref->nid) {
      if ($task = hosting_get_most_recent_task($ref->nid, 'install')) {
        drush_log(dt('Removed the dummy install task: @nid.', array('@nid' => $task->nid)), 'ok');
        _computerminds_migrate_node_delete($task->nid);
      }
    }

    drush_log(dt('The site: @uri has been imported.', array('@uri' => $site)), 'ok');
  }
}

/**
 * Helper function to remove a node from the frontend Aegir server.
 *
 * This is a copy of node_delete() from Drupal 6, but minus the access check on
 * the current user.
 */
function _computerminds_migrate_node_delete($nid) {

  // Clear the cache before the load, so if multiple nodes are deleted, the
  // memory will not fill up with nodes (possibly) already removed.
  $node = node_load($nid, NULL, TRUE);
  
  // Skip the access check that should be here.
  // if (node_access('delete', $node)) {
  if (TRUE) {
    db_query('DELETE FROM {node} WHERE nid = %d', $node->nid);
    db_query('DELETE FROM {node_revisions} WHERE nid = %d', $node->nid);
    db_query('DELETE FROM {node_access} WHERE nid = %d', $node->nid);

    // Call the node-specific callback (if any):
    node_invoke($node, 'delete');
    node_invoke_nodeapi($node, 'delete');

    // Clear the page and block caches.
    cache_clear_all();

    // Remove this node from the search index if needed.
    if (function_exists('search_wipe')) {
      search_wipe($node->nid, 'node');
    }
    watchdog('content', '@type: deleted %title.', array('@type' => $node->type, '%title' => $node->title));
    drupal_set_message(t('@type %title has been deleted.', array('@type' => node_get_types('name', $node), '%title' => $node->title)));
  }
}

/**
 * Drush command to migrate a lot of sites from an old Hostmaster to this one.
 */
function drush_computerminds_migrate_migrate_computerminds_all() {
  // Get the remote sites.
  $remote_sites = computerminds_migrate_get_all_sites('@' . COMPUTERMINDS_OLD_HOSTMASTER_NAME, COMPUTERMINDS_PLATFORM_NAME);

  // Get the local sites.
  $local_sites = computerminds_migrate_get_all_sites('@hostmaster', COMPUTERMINDS_PLATFORM_NAME);

  // Migrate the diff.
  $sites_to_migrate = array_diff($remote_sites, $local_sites);
  
  $limit = drush_get_option('limit', 0);
  // Truncate the list of sites to migrate if there is a limit.
  if (!empty($limit)) {
    $sites_to_migrate = array_slice($sites_to_migrate, 0, $limit);
  }

  drush_log(dt('The following sites will be migrated:'), 'ok');

  foreach ($sites_to_migrate as $site) {
    drush_log('  ' . $site, 'ok');
  }

  if (!drush_confirm(dt('Do you want to proceed.'))) {
    return;
  }

  drush_log(dt('Migrating...'), 'ok');

  $success = array();
  $failed = array();
  
  $count = 0;

  foreach ($sites_to_migrate as $site) {
    $result = provision_backend_invoke('@hostmaster', 'migrate-computerminds-one', array($site));
    $count++;
    drush_log(dt('@count of @total sites migrated...', array('@count' => $count, '@total' => count($sites_to_migrate))), 'ok');
    if (!empty($result['error_status'])) {
      $failed[] = $site;
    }
    else {
      $success[] = $site;
    }
  }

  if (!empty($success)) {
    drush_log(dt('The following sites migrated successfully'), 'ok');
    foreach ($success as $site) {
      drush_log('  ' . $site, 'ok');
    }
  }

  if (!empty($failed)) {
    drush_log(dt('The following sites migrated unsuccessfully'), 'error');
    foreach ($failed as $site) {
      drush_log('  ' . $site, 'error');
    }
  }
}

/**
 * Get all sites of a given platform on a given server.
 */
function computerminds_migrate_get_all_sites($target, $platform) {
  $platform = 'platform_' . $platform;
  // Get a list of all the sites on the remote hostmaster.
  $result = drush_backend_invoke_args('@' . ltrim($target, '@') . ' ' . 'platform-list-sites', array($platform), array('root' => NULL, 'uri' => NULL), 'GET', FALSE);
  $sites = unserialize($result['output']);
  if (is_array($sites)) {
    return $sites;
  }
  return array();
}
