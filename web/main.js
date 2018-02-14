
var mainAppClass = function () {
    console.group("main application entry point");
    this.construct();
};

var socketQueueId = 0;
var socketQueue = {};
function sendData(data, onReturnFunction){
    socketQueueId++;
    if (typeof(returnFunc) == 'function'){
        // the 'i_' prefix is a good way to force string indices, believe me you'll want that in case your server side doesn't care and mixes both like PHP might do
        socketQueue['i_'+socketQueueId] = onReturnFunction;
    }
    jsonData = JSON.stringify({'cmd_id':socketQueueId, 'json_data':data});
    try{
        webSocket.send(jsonData);
        console.log('Sent');
    }catch(e){
        console.log('Sending failed ... .disconnected failed');
    }
}

mainAppClass.prototype = {
    WS_PROTOCOL: "websocket-protocol",
    WS_URL: (location.protocol == "https:" ? "wss" : "ws") + "://" + document.location.hostname + ":7688",
    socketQueueId: 0,
    socketQueue: {},

    sendData: function(data, onReturnFunction) {
        var self = this;

        self.socketQueueId++;
        
        if (typeof(returnFunc) == 'function') {
            socketQueue['i_'+ self.socketQueueId] = onReturnFunction;
        }

        jsonData = JSON.stringify({'cmd_id': self.socketQueueId, 'json_data': data});
        
        try {
            self.ws.send(jsonData);
            console.log('Sent');
        } catch(e) {
            console.log('Sending failed ... .disconnected failed');
        }
    },
    socketRecieveData(data){
        var self = this;
        console.log("socketRecieveData");
    },
    connect: function() {
        var self = this;

        console.log("websocket connect")

        if (window.WebSocket) {
			self.ws = new WebSocket(self.WS_URL, self.WS_PROTOCOL);
		} else if (window.MozWebSocket) {
			self.ws =  new MozWebSocket(self.WS_URL, self.WS_PROTOCOL);
        }

        self.setupHandler();

        self.ws.onclose = function() {
            setTimeout(function() {
                self.connect()
            }, 1000);
        };
    },
    setupHandler: function() {
        var self = this;

        self.ws.onmessage = function(e) {
            try {
                data = JSON.parse(e.data);
            } catch(er) {
                console.log('socket parse error: '+e.data);
            }

            if (typeof(data['cmd_id']) != 'undefined' && typeof(socketQueue['i_' + data['cmd_id']]) == 'function') {
                execFunc = socketQueue['i_' + data['cmd_id']];

                execFunc(data['result']);

                delete socketQueue['i_' + data['cmd_id']];
                return;
            } else {
                socketRecieveData(e.data);
            }
        }        
    },
    construct: function () {
        var self = this;
        console.group("main application construct");

		self.connect();
    }
};

app = new mainAppClass();
