function postCsv(buf, cb) {
    var client = new XMLHttpRequest();
    var url = "https://nct.azurewebsites.net/data" +
                "?watch=" + encodeURIComponent(Pebble.getWatchToken()) + 
                "&account=" + encodeURIComponent(Pebble.getAccountToken());
    client.onreadystatechange = function () {
        if (client.readyState == 4) {
            console.log("Server response", client.status, client.responseText, url);
            cb(client.status);
        }
    };
    client.open("POST", url);
    client.setRequestHeader("content-type", "text/csv; charset=utf-8");
    client.send(buf);
}

function decode(arr) {
  var out = "";
  var ind = 0;
  function g(off,k) {
    var v = (arr[off+1] << 8) | (arr[off]);
    ind += ((v >> 13) & 7) << k;
    return (v << 19) >> 19;
  }
  for(var i = 0; i < arr.length; i += 6) {
    ind = 0;
    var x = g(i+0, 0);
    var y = g(i+2, 3);
    var z = g(i+4, 6);
    out += x + "," + y + "," + z + "," + ind*250 + "\n";
  }
  return out;
}

function sendMsg(dict) {
    var name = Object.keys(dict).join(",");
    Pebble.sendAppMessage(dict, function(e) {
        console.log('Sent message, ' + name);
    }, function(e) {
        console.log('Send failed, message, ' + name);
    });
}

Pebble.addEventListener('ready', function(e) {
    console.log('ProCounter JS ready');
    sendMsg({ 'KEY_APP_READY':1 });
});

var csv = "";

Pebble.addEventListener('appmessage', function(e) {
    var data = e.payload.KEY_ACC_DATA || [];
    csv += decode(data);
    
    if (e.payload.KEY_POST_DATA && csv) {
        var tosend = "X,Y,Z,I\n" + csv;
        csv = "";
        postCsv(tosend, function(status) {
            sendMsg({ KEY_DATA_POSTED: status });
        });
    }
});
