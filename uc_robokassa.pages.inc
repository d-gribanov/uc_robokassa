<?php

/**
 * Processes Instant Payment Notifiations from Robokassa.
 */
function uc_robokassa_ipn() {
  if (!isset($_POST['InvId'])) {
    watchdog('uc_robokassa', 'IPN attempted with invalid order ID.', array(), WATCHDOG_ERROR);
    return;
  }

  // Sanitize order ID and cart ID
  $order_id = intval($_POST['InvId']);

  watchdog('uc_robokassa', 'Receiving IPN at URL for order @order_id.', array('@order_id' => $order_id));

  $order = uc_order_load($order_id);

  if ($order == FALSE) {
    watchdog('uc_robokassa', 'IPN attempted for non-existent order @order_id.', array('@order_id' => $order_id), WATCHDOG_ERROR);
    return;
  }

  // Assign posted variables to local variables
  $payment_amount = check_plain($_POST['OutSum']);
  $received_signature = strtoupper(check_plain($_POST['SignatureValue']));

  $check_signature = strtoupper(md5(
    uc_currency_format($order->order_total, FALSE, FALSE, '.') . ':' .
    $order_id . ':' .
    variable_get('uc_robokassa_MerchantPass2', '')
  ));

  if ($received_signature != $check_signature) {
    watchdog('uc_robokassa', 'IPN transaction failed verification.', array(), WATCHDOG_ERROR);
    uc_order_comment_save($order_id, 0, t('An IPN transaction failed verification for this order.'), 'admin');
    return;
  }

  //succeful IPN
  watchdog('uc_robokassa', 'IPN transaction verified.');

  $duplicate = (bool) db_select('uc_payment_robokassa_ipn')
    ->fields('uc_payment_robokassa_ipn')
    ->condition('order_id', $order_id)
    ->countQuery()
    ->execute()
    ->fetchField();

  if ($duplicate) {
    watchdog('uc_robokassa', 'IPN transaction ID has been processed before.', array(), WATCHDOG_NOTICE);
    return;
  }

  db_insert('uc_payment_robokassa_ipn')
    ->fields(array(
      'order_id' => $order->order_id,
      'mc_gross' => $payment_amount,
      'received' => REQUEST_TIME,
    ))
    ->execute();

  if (abs($payment_amount - $order->order_total) > 0.01) {
    watchdog(
      'uc_robokassa',
      'Payment for order @order_id did not equal the order total.',
      array('@order_id' => $order->order_id),
      WATCHDOG_WARNING,
      l(t('view'), 'admin/store/orders/' . $order->order_id)
    );
  }
  $comment = t('Robokassa transaction order ID: @order_id', array('@order_id' => $order->order_id));
  uc_payment_enter($order_id, 'robokassa', $payment_amount, $order->uid, NULL, $comment);
  uc_cart_complete_sale($order);
  uc_order_comment_save(
    $order_id,
    0,
    t('Robokassa IPN reported a payment of @amount.', array('@amount' => uc_currency_format($payment_amount, FALSE)))
  );
  return 'OK'.$order->order_id;
}

/**
 * Handles a complete Website sale.
 */
function uc_robokassa_complete() {
  $get = drupal_get_query_parameters();
  $order_id = intval($get['InvId']);
  // If the order ID specified in the return URL is not the same as the one in
  // the user's session, we need to assume this is either a spoof or that the
  // user tried to adjust the order on this side while at Robokassa. If it was a
  // legitimate checkout, the IPN will still come in from Robokassa so the order
  // gets processed correctly. We'll leave an ambiguous message just in case.
  if (!isset($_SESSION['cart_order']) || intval($_SESSION['cart_order']) != $order_id) {
    drupal_set_message(t('Thank you for your order! Robokassa will notify us once your payment has been processed.'));
    drupal_goto('cart');
  }
  $order = uc_order_load($order_id);
  // Ensure the payment method is Robokassa.
  if ($order->payment_method != 'robokassa') {
    drupal_goto('cart');
  }

  // This lets us know it's a legitimate access of the complete page.
  $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
  drupal_goto('cart/checkout/complete');
}

/**
 * Handles a canceled Website Payments sale.
 */
function uc_robokassa_cancel() {
  unset($_SESSION['cart_order']);

  drupal_set_message(t('Your Robokassa payment was canceled. Please feel free to continue shopping or contact us for assistance.'));

  drupal_goto('cart');
}
