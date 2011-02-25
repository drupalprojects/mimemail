<?php

/**
 * @file
 * Common mail functions for queueing and sending e-mail.  Originally 
 * written by Gerhard.  
 * 
 *  see http://cvs.drupal.org/viewcvs/drupal/contributions/sandbox/killes/mail/
 * 
 * I have adapted this code and made the following changes:
 *   - Deleted mail_queue_* functions 
 *   - added mail_rfc_headers to generate a header string for use in 
 *     both the message headers and mime-part headers
 *   - modified mail_prepare_headers to use mail_rfc_headers
 *   - refactored mail_multipart_body to eliminate some duplicate logic
 *     and to use the mail_rfc_headers function for mime parts.
 *   - Reworked preg_match rules in mail_extract_files, as it was 
 *     breaking some formats ('<img src=' worked but '<img align="left" src=' didn't)
 *   
 *   Allie Micka < allie at pajunas dot com >
 */

/**
 * Attempts to RFC822-compliant headers for the mail message or its MIME parts
 * TODO could use some enhancement and stress testing 
 * 
 * @param $headers An array of headers
 * @return header string
 */
function mimemail_rfc_headers($headers) {
  $header = '';
  foreach ($headers as $key => $value) {
    $key = trim($key);
    // collapse spaces and get rid of newline characters
    $value = preg_replace('/(\s+|\n|\r|^\s|\s$)/',' ',$value);
    
    //fold headers if they're too long
    if (strlen($value) > 60) {
      //if there's a semicolon, use that to separate
      if (count($array = preg_split('/;\s*/',$value)) > 1) {
        $value = trim(join(";\n    ", $array));
      } 
      else {
        $value = wordwrap($value,50,"\n    ",false);
      }
    }
    $header .= "$key: $value\n";
  }
  return trim($header);
}

/**
 * Formats an address string 
 * TODO could use some enhancement and stress testing 
 * 
 * @param $address - a user object, a text email address or an array containing name, mail
 * @return formatted address string
 */
function mimemail_address($address) {
  
  if (is_array($address)) {
    
    // it's an array containing 'mail' and/or 'name'
    if (isset($address['mail'])) {
      $output = '';
      if (empty($address['name'])) {
        return $address['mail'];
      } else {
        return '"'.addslashes($address['name']).'" <'.$address['mail'].'>';
      }
    }
    
    // it's an array of address items
    $addresses = array();
    foreach($address as $a) {
      $addresses[] = mimemail_address($a);
    }
    return $addresses;
  }
  
  // it's a user object
  if (is_object($address) && isset($address->mail)) {
    return '"'.addslashes($address->name).'" <'.$address->mail.'>';
  }
  
  // it's formatted or unformatted string
  // TODO shouldn't assume it's valid - should try to re-parse
  if (is_string($address)) {
    return $address;
  }
  return false;
}

/**
 * Gives useful defaults for standard email headers.
 *
 * @param $headers An array of headers
 * @return header string.
 */
function mimemail_prepare_headers($headers,$from='') {
  // Note: This may work or not. The MTA might rewrite the Return-Path, and Errors-To is deprecated.
  // Depending on your MTA and its configuration you can make it work by specifying appropriate arguments in $additional_parameters
  if (!$from) {
    $from = variable_get('site_mail', ini_get('sendmail_from'));
  }
  preg_match('/[a-z0-9\-\.]+@{1}[a-z0-9\-\.]+/i', $from, $matches);
  $from_email = $matches[0];
  // allow a mail to overwrite standard headers.
  $headers = array_merge(array('Return-Path' => "<$from_email>", 'Errors-To' => $from, 'From' => $from, 'Content-Type' => 'text/plain; charset=utf-8; format=flowed'),$headers);
  return mimemail_rfc_headers($headers);
}

/**
 * Converts html to utf-8 encoded text.
 *
 * @param $txt html text that needs formatting.
 * @param $inline Optional. If TRUE put links in the text, 
 *               if FALSE put a footnote into the text and
 *               a list of links below it. Default: FALSE
 *
 * @return formatted text encoded in utf-8
 */
function mimemail_html_to_text($txt, $inline = FALSE) {
  $pattern = '@(<a href="(.\S+?)"[^>]*>(.+?)</a>)@emi';
  if ($inline) {
    $txt = preg_replace($pattern, "_mimemail_uri('\\2', '\\3')", $txt);
  }
  else {
    $txt = preg_replace($pattern, "'\\3 ['. _mimemail_urls('\\2') .']'", $txt);
    $urls = _mimemail_urls();
    if (count($urls)) {
      $txt .= "\n";
      $i = 0;
      for ($max = count($urls); $i < $max; $i++) {
        $txt .= '['. ($i + 1) .'] '. $urls[$i] ."\n";
      }
    }
    _mimemail_urls(0, TRUE);
  }

  // some basic html to text conversion
  $txt = preg_replace(_mimemail_define_search(), _mimemail_define_replace(), $txt);
  $txt = preg_replace("/\n\s+\n/", "\n\n", $txt);
  $txt = strip_tags($txt);
  $txt = decode_entities($txt);
  return wordwrap($txt, 80);
}

/**
 * Extracts links to local images from html documents.
 *
 * @param $html html text
 * @param $name document name
 *
 * @return an array of arrays
 *            array(array(
 *                     'name' => document name
 *                     'content' => html text, local image urls replaced by Content-IDs,
 *                     'Content-Type' => 'text/html; charset=utf-8')
 *                  array(
 *                     'name' => file name,
 *                     'file' => reference to local file,
 *                     'Content-ID' => generated Content-ID,
 *                     'Content-Type' => derived using mime_content_type 
 *                                       if available, educated guess otherwise
 *                     )
 *                  )
 */
function mimemail_extract_files($html, $name) {
  $pattern = '/(<link[^>]+href="?|<object[^>]+codebase="?|@import |src="?)\/?([^"]+)("?)/emis';
  $html = preg_replace($pattern,'"\\1". _mimemail_file("\\2") ."\\3"', $html);
  
  $document = array(array(
    'Content-Type' => 'text/html; charset=utf-8',
    'Content-Transfer-Encoding' => 'base64',
    'content' => chunk_split(base64_encode($html)),
  ));
  
  $files = _mimemail_file();
  
  return array_merge($document, $files);
}

/**
 * Helper function to extract local files
 *
 * @param $file a link to a file
 *
 * @return an absolute :
 */
function _mimemail_file($file = NULL, $type = '') {
  static $files = array();

  if ($file && !preg_match('@://|mailto:@', $file) && file_exists($file)) {
    $content_id = md5($file) .'@'. $_SERVER['HTTP_HOST'];

    $new_file = array('name' => substr($file, strrpos($file, '/') + 1),
                      'file' => $file,
                      'Content-ID' => $content_id,
                      'Content-Disposition' => 'related',
                     );
    $new_file['Content-Type'] = _mimemail_mimetype($file,$type);

    $files[] = $new_file;

    return 'cid:'. $content_id;
  }
  
  if ($file) {
    return $file;
  }
  
  return $files;
}

/**
 *
 * @param $parts 
 *        an array of parts to be included
 *        each part is itself an array:
 *        array(
 *          'name' => $name the name of the attachement
 *          'content' => $content textual content
 *          'file' => $file a file
 *          'Content-Type' => Content type of either file or content.
 *                            Mandatory for content, optional for file.
 *                            If not present, it will be derived from
 *                            file the file if mime_content_type is available.
 *                            If not, application/octet-stream is used.
 *          'Content-Disposition' => optional, inline is assumed
 *          'Content-Transfer-Encoding' => optional,
 *                                         base64 is assumed for files
 *                                         8bit for other content.
 *          'Content-ID' => optional, for in-mail references to attachements.
 *        )
 *        name is mandatory, one of content and file is required,
 *        they are mutually exclusive.
 *
 * @param $content_type
 *        Content-Type for the combined message, optional, default: multipart/mixed
 *
 * @return
 *     an array containing the elements 'header' and 'body'.
 *     'body' is the mime encoded multipart body of a mail.
 *     'headers' is an array that includes some headers for the mail to be sent.
 */
function mimemail_multipart_body($parts, $content_type = 'multipart/mixed; charset=utf-8',$sub_part=false) {
  $boundary = md5(uniqid(time()));
  $body = '';
  $headers = array(
    'Content-Type' => "$content_type; boundary=\"$boundary\"",
  );
  if (!$sub_part) {
    $headers['MIME-Version'] = '1.0'; 
    $body = "This is a multi-part message in MIME format.\n";
  }

  foreach ($parts as $part) {
    $part_headers = array();
    
    if (isset($part['Content-ID'])) {
      $part_headers['Content-ID'] = '<'. $part['Content-ID'] .'>';
    }
    
    if (isset($part['Content-Type'])) {
      $part_headers['Content-Type'] = $part['Content-Type'];
    }
    
    if (isset($part['Content-Disposition'])) {
      $part_headers['Content-Disposition'] = $part['Content-Disposition'];
    }
    else { 
      $part_headers['Content-Disposition'] = 'inline';
    }
    
    if ($part['Content-Transfer-Encoding']) {
      $part_headers['Content-Transfer-Encoding'] = $part['Content-Transfer-Encoding'];
    }
    
    // mail content provided as a string
    if (isset($part['content']) && $part['content']) {
      if (!isset($part['Content-Transfer-Encoding'])) {
        $part_headers['Content-Transfer-Encoding'] = '8bit';
      }
      $part_body = $part['content'];
      if (isset($part['name'])) {
        $part_headers['Content-Type'] .= '; name="'.$part['name'].'"';
        $part_headers['Content-Disposition'] .= '; filename="'.$part['name'].'"';
      }
      
    // mail content references in a filename
    } 
    else {
      if (!isset($part['Content-Transfer-Encoding'])) {
        $part_headers['Content-Transfer-Encoding'] = 'base64';
      }
      
      if (!isset($part['Content-Type'])) {
        $part['Content-Type'] = _mimemail_mimetype($part['file'],$type);
      }

      if (isset($part['name'])) {
        $part_headers['Content-Type'] .= '; name="'.$part['name'].'"';
        $part_headers['Content-Disposition'] .= '; filename="'.$part['name'].'"';
      }
      
      $part_body = chunk_split(base64_encode(file_get_contents($part['file'])));
    }
    
    $body .= "\n--$boundary\n";
    $body .= mimemail_rfc_headers($part_headers)."\n\n";
    $body .= $part_body;
  }
  $body .= "\n--$boundary--\n";

  return array('headers' => $headers, 'body' => $body);
}

/**
 * Generate a multipart message body with a text alternative for some html text
 * @param $body An HTML message body
 * @param $subject The message subject
 * @param $plaintext Whether the recipient prefers plaintext-only messages (default false)
 * 
 * @return
 *     an array containing the elements 'header' and 'body'.
 *     'body' is the mime encoded multipart body of a mail.
 *     'headers' is an array that includes some headers for the mail to be sent.
 *
 * The first mime part is a multipart/alternative containing mime-encoded
 * sub-parts for HTML and plaintext.  Each subsequent part is the required
 * image/attachment
 */
function mimemail_html_body($body, $subject, $plaintext=false) {
  //generate plaintext alternative
  $text = mimemail_html_to_text($body);
  if ($plaintext) {
    return $text;
  }
  $text_part = array('Content-Type'=>'text/plain; charset=utf-8','content'=>$text);
  
  //expand all local links
  $pattern = '/(<a[^>]+href=")([^"]*)/emi';
  $body = preg_replace($pattern, '"\\1"._mimemail_url("\2")', $body);
  
  $mime_parts = mimemail_extract_files($body, $subject);

  $content = array($text_part,array_shift($mime_parts)); 
  $content = mimemail_multipart_body($content,'multipart/alternative',true);
  $content_part = array('Content-Type'=>$content['headers']['Content-Type'],'content'=>$content['body']);

  $parts = array_merge(array($content_part), $mime_parts);
  return mimemail_multipart_body($parts,'multipart/related; charset=utf-8');
}

/**
 * Helper function to format urls
 *
 */
function _mimemail_uri($href, $link) {
  $href = _mimemail_url($href);
  if ($href == $link) {
    $output =  '['.$href.']';
  }
  else {
    $output = $link.' ['.$href.']';
  }
  return $output;
}

/**
 * Helper function to format urls
 *
 * @param $url an url
 *
 * @return an absolute url, sans mailto:
 */
function _mimemail_url($url) {
  if (strpos($url, '://')) {
    return $url;
  }
  
  if (preg_match('!mailto:!i', $url)) {
    return str_replace('mailto:', '', $url);
  }
  
  return url($url, NULL, NULL, TRUE);
}

/**
 * Helper function to store processed urls.
 *
 * @param $url Optional. 
 * @param $refresh Optional. If TRUE refresh the cache.
 *
 * @return a count of stored if $url evaluates to false, the stored urls ortherwise.
 */
function _mimemail_urls($url = 0, $refresh = FALSE) {
  static $urls = array();

  if($refresh) {
    $urls = array();
  }

  if ($url) {
    $urls[] = _mimemail_url($url);
    return count($urls);
  }

  return $urls;
}

/**
 *  List of preg* regular expression patterns to search for,
 *  used in conjunction with $replace.
 *  Based on / modified from html2txt.module
 */
function _mimemail_define_search() {

    $search = array(
        "/\r/",                                       // Non-legal carriage return
        "/[\t]+/",                                    // tabs
        '/<script[^>]*>.*?<\/script>/i',              // <script>s -- which strip_tags supposedly has problems with
        '!<h[12][^>]*>(.+?)</h[12]>!ie',              // h1 - h2
        '/<h3[^>]*>(.+?)<\/h3>/ie',                   // h3
        '/<h[456][^>]*>(.+?)<\/h[456]>/ie',           // h4 - h6
        '!<blockquote[^>]*>!i',                       // <blockquote>
        '!</blockquote>!i',                           // </blockquote>
        '!</?p[^>]*>!i',                              // <p>
        '/<br[^>]*>/i',                               // <br>
        '!</?(b|strong)[^>]*>!i',                     // <b> and <strong>
        '!</?(em|i)[^>]*>!i',                         // <em> and <i>
        '/(<ul[^>]*>|<\/ul>)/i',                      // <ul> and </ul>
        '/(<ol[^>]*>|<\/ol>)/i',                      // <ol> and </ol>
        '/(<dl[^>]*>|<\/dl>)/i',                      // <dl> and </dl>
        '!<li[^>]*>!i',                               // <li>
        '!</li>!i',                                   // </li>
        '!<dt[^>]*>!i',                               // <dt>
        '!</dt>!i',                                   // </dt>
        '!<dd[^>]*>!i',                               // <dd>
        '!</dd>!i',                                   // </dd>
        '/<hr[^>]*>/i',                               // <hr>
        '/(<table[^>]*>|<\/table>)/i',                // <table> and </table>
        '/(<tr[^>]*>|<\/tr>)/i',                      // <tr> and </tr>
        '/<td[^>]*>(.+?)<\/td>/i',                    // <td> and </td>
        '/<th[^>]*>(.+?)<\/th>/i',                    // <th> and </th>
        '/&nbsp;/i',
        '/&quot;/i',
        '/&gt;/i',
        '/&lt;/i',
        '/&amp;/i',
        '/&copy;/i',
        '/&trade;/i',
        '/&#8220;/',
        '/&#8221;/',
        '/&#8211;/',
        '/&#8217;/',
        '/&#38;/',
        '/&#169;/',
        '/&#8482;/',
        '/&#151;/',
        '/&#147;/',
        '/&#148;/',
        '/&#149;/',
        '/&reg;/i',
        '/&bull;/i',
        '/&[&;]+;/i'
    );

    return $search;
}

/**
 *  List of pattern replacements corresponding to patterns searched.
 *  Based on / modified from html2txt.module
 */
function _mimemail_define_replace() {

    $replace = array(
        '',                                                               // Non-legal carriage return
        ' ',                                                              // Tabs
        '',                                                               // <script>s -- which strip_tags supposedly has problems with
        "\"\n------------\" .'\\1'.\"------------\n\n\"",                 // h1 - h2
        "\"\n\n\\1\n\n\"",                                                // h3
        "\"\n\\1\n\"",                                                    // h4 - h6
        "\n\n\t",                                                         // <blockquote>
        "\n\n",                                                           // </blockquote>
        "\n\n",                                                           // <p>
        "\n",                                                             // <br>
        '*',                                                              // <b> and <strong>
        '/',                                                              // <em> and <i>
        "\n\n",                                                           // <ul> and </ul>
        "\n\n",                                                           // <ol> and </ol>
        "\n\n",                                                           // <dl> and </dl>
        "  * ",                                                           // <li>
        "\n",                                                             // </li>
        "  * ",                                                           // <dt>
        "\n",                                                             // </dt>
        "\t",                                                             // <dd>
        "\n",                                                             // </dd>
        "\n-------------------------\n",                                  // <hr>
        "\n\n",                                                           // <table> and </table>
        "\n",                                                             // <tr> and </tr>
        "\t\t\\1\n",                                                      // <td> and </td>
        "\"\t\t\\1\n\"",                                      // <th> and </th>
        ' ',
        '"',
        '>',
        '<',
        '&',
        '(c)',
        '(tm)',
        '"',
        '"',
        '-',
        "'",
        '&',
        '(c)',
        '(tm)',
        '--',
        '"',
        '"',
        '*',
        '(R)',
        '*',
        ''
    );
    return $replace;
}

/**
 * Attempt to determine the mimetime from or filename .  While not ideal, 
 * using the filename as a fallback ensures that images will appear inline
 * in HTML messages
 *
 * @param $name Name of the file
 * 
 * @return best-guess mimetype string
 */
function _mimemail_mimetype($file,$type='') {
  if ($type) {
    return $type;
  } 

  if (function_exists('mime_content_type')) {
      return mime_content_type($file);
  }

  // some common embedded/attachment types
  $types = Array( 
    'jpg'  => 'image/jpeg',
    'jpeg' => 'image/jpeg',
    'gif'  => 'image/gif',
    'png'  => 'image/png',
  );
  $ext = strtolower(substr(strrchr($file, '.'), 1));

  if (isset($types[$ext])) {
    return $types[$ext];
  }

  return 'application/octet-stream';
}
