<?php
// $Id: notifications_event.class.inc,v 1.1.2.13 2010/07/22 11:57:49 jareyero Exp $
/**
 * @file
 * Drupal Notifications Framework - Default class file
 */


/**
 * Notifications Schedule Event class
 * 
 * Unlike regular events, these are full auto-contained events that know which message to produce
 * and how to send it. Thus, these events will be queued in regular Drupal queue
 */
abstract class Notifications_Scheduler_Event extends Notifications_Event {
  // Default system queue name and item if came from queue
  public $queue = 'notifications_event';
  protected $queue_item;
  // Subscriptions counter, this starts with 1 (si the event record is not deleted)
  // And with every process is set to the max sid processed
  public $counter = 1;
  // Subscriptions to be processed on every batch, indexed by sid
  protected $subscriptions;
  // Store action parameters
  protected $action_object;
  protected $accion_context;
  
  /**
   * Trigger this event
   */
  function trigger() {
    // Load the content now, later it may be different
    if ($this->content = $this->load_content()) {
      // Build message template so it can be reused later
      if ($this->get_template()) {
        // Always queue for later processing
        //$this->queue();
        //return TRUE;
        return parent::trigger();
      }
    }
    // If something failed we don't even store this event
    return FALSE;
  }
  /**
   * Prepare event objects from context parameters
   */
  public abstract function prepare_context();
  /**
   * Load content, to be implemented by subclasses
   */
  public abstract function load_content();

  /**
   * Build template, to be implemented by subsclasses
   */
  public abstract function build_template();

  /**
   * Action info to return on hook_action_info()
   */
  public function get_action_info() {
    return array(
      'type' => 'job_scheduler',
      'label' => $this->get_action_label(),
      'configurable' => $this->is_configurable(),
      'behavior' => array('sends_notification'),
      'triggers' => array('any'),
    );
  }
  /**
   * Action label, build default with event label
   */
  public function get_action_label() {
    return t('Scheduled notification: @name', array('@name' =>  $this->get_title()));
  }
  /**
   * Whether the action is configurable. Forms handling must be provided by the module
   */
  public function is_configurable() {
    return FALSE;
  }
  /**
   * Set action parameters
   */
  public function set_action($object, $context) {
    $this->action_object = $object;
    $this->action_context = $context;
    $this->set_fields();
    return $this;
  }
  /**
   * Get property from action context
   */
  public function get_action_context($name = NULL, $default = NULL) {
    if ($name) {
      return isset($this->action_context[$name]) ? $this->action_context[$name] : $default;
    }
    else {
      return $this->action_context;
    }
  }
  /**
   * Process event, send pending notifications. Subscriptions start on $counter (min sid)
   * 
   * @param $limit
   *   Maximum number of subscriptions to process
   */
  function process($limit = 10) {
    while ($limit && ($subscriptions = $this->get_subscriptions($limit))) {
      $limit = $limit - count($subscriptions);
      // New value for the counter if this goes right
      $counter = max(array_keys($subscriptions));
      // Turn subscriptions into batches, groups, etc...
      $groups = $this->prepare_subscriptions($subscriptions);
      // Now process groups one at a time
      foreach ($groups as $group) {
        $results = $this->process_group($groups);
      }
      // Update counter on the event record
      $this->update_counter($counter);
    }
    if (!empty($counter)) {
      // We will do more processing later
      $this->release();
    }
    else {
      // Nothing to process, delete all this
      $this->delete();
    }
  }

  /**
   * Groups subscriptions. This one will just create a group for all
   */
  protected function prepare_subscriptions($subscriptions) {
    return array($subscriptions);
  }

  /**
   * Process group, add all to a message and send out
   */
  protected function process_group($group) {
    $result = array('messages' => 0, 'items' => 0, 'errors' => 0);
    $message = $this->build_message();
    foreach ($group as $item) {
      if ($destination = $item->get_destination()) {
        $message->add_destination($destination);
      }
      else {
        $result['errors']++;
      }
      $result['items']++;
    }
    if ($message->send()) {
      $result['messages']++;
    }
    return $result;
  }


}


/**
 * Test this schedule class, send latest created nodes
 */
class Notifications_Scheduler_Latest_Posts extends Notifications_Scheduler_Event {
  /**
   * Set action parameters, get field mapping from context
   */
  protected function prepare_context() {
    if ($type = $this->get_action_context('type')) {
      $this->add_object('node_type', $type);
    }
    // Some hack, try a field with array value ??
    if ($tid = $this->get_action_context('term')) {
      $this->add_object('taxonomy_term', $term);
    }
  } 
  /**
   * Load latest nodes created. It doesn't check node access
   */
  protected function load_content() {
    $select = db_select('node', 'n')
      ->fields('n', array('nid', 'created'))
      ->condition('status', 1)
      ->orderBy('created', 'DESC');
      ->limit($this->get_action_context('number', variable_get('default_nodes_main', 10));
    if ($type = $this->get_action_context('type')) {
      $select->condition('type', $type);
    }
    if ($tid = $this->get_action_context('term')) {
      $query->join('taxonomy_index', 't', 'n.nid = t.nid');
      $query->condition('t.tid', $tid);      
    }
    $nids = $select->execute()->fetchCol();
    return node_load_multiple($nids);
  }
  /**
   * Build message template. It doesn't check node access
   */
  protected function build_template() {
    $template = $this->get_template('message');
    $template->add_string('subject', $this->get_subject());
    $body = $template->add_template('body', $this->get_template('body'));
    $body->add_item('header', $this->get_header());
    $content = $body->add_element('content', $this->get_template('content'));
    $body->add_item('footer', $this->get_footer());
    // All nodes will be stored, the template will possibly render them with node_view_multiple()
    foreach ($this->content as $nid => $node) {
      $content->add_item($nid, $node);
    }
    return $template;
  }
  
}

/**
 * Template for a list of nodes, list of lins
 */
class Notifications_Scheduler_Node_List extends Notifications_Template {

  /**
   * Default texts for this template, may need token replacement
   */
  function default_text($type, $options) {
    switch ($type) {
      case 'subject':
        return t('Latest posts from [site:name]', array(), $options);
      case 'content':
        // @todo There should be a way to format this as an item list
        $list = array('#type' => 'messaging_text');
        foreach ($this->event->get_content() as $node) {
          $list[$node->nid] = array(
            '#type' => 'link',
            '#title' => $node->title,
            '#href' => 'node/' . $node->nid,
            '#options' => array('absolute' => TRUE),
          );
        }
        return $list;
      default:
        return parent::default_text($type, $options);
    }
  }  
}

/**
 * Template for a list of nodes, teasers
 */
class Notifications_Scheduler_Node_Teasers extends Notifications_Scheduler_Node_List {
   /**
   * Default texts for this template, may need token replacement
   */
  function default_text($type, $options) {
    switch ($type) {
      case 'content':
        return node_view_multiple($this->event->get_content());
      default:
        return parent::default_text($type, $options);
    }
  } 
}