
var connection;
var colorSet = 'M';
init();
function init(){
    connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);
}


connection.onopen = function () {
    connection.send('Connect ' + new Date());
    document.getElementById('connectionStatus').textContent = "Connected"
    document.getElementById('connectionStatus').style = "color:lime;"
};
connection.onerror = function (error) {
    console.log('WebSocket Error ', error);
    document.getElementById('connectionStatus').textContent = "Not Connected"
    document.getElementById('connectionStatus').style = "color:red;"
};
connection.onmessage = function (e) {  
    console.log('Server: ', e.data);
    if (e.data.substring(0,1) == "T"){
        document.getElementById('timeout').value = e.data.substring(1);
    }
};
connection.onclose = function(){
    console.log('WebSocket connection closed');
    document.getElementById('connectionStatus').textContent = "Not Connected"
    document.getElementById('connectionStatus').style = "color:red;"
};

function changeStairs(){
    console.log(window.location.hostname);
    if (window.location.hostname.includes("131")){
        location.replace("http://192.168.1.132");
    }else if (window.location.hostname.includes("132")){
        location.replace("http://192.168.1.131");
    }
}

function sendRGB() {
    //console.log(connection);
 //   if (connection.readyState ==3) init();
    var r = colorPicker.color.rgb.r
    var g = colorPicker.color.rgb.g
    var b = colorPicker.color.rgb.b
    
    var rgb = r << 20 | g << 10 | b;
    var rgbstr = 'C'+colorSet+ rgb.toString(16);    
    console.log('RGB: ' + rgbstr); 
    connection.send(rgbstr);
    var brightnessValue = Math.round(colorPicker.color.hsv.v * 0.9);
    var brightnessString = "B" + brightnessValue;
    console.log('B: ' + brightnessString); 
    connection.send(brightnessString);
    console.log()
}

function sendButton(sendMode){
    var modeString = "M" + sendMode;
    console.log(modeString);
    connection.send(modeString);
}

function sendTimeout(){
    var timeoutString = "T" +  Math.round(document.getElementById('timeout').value**2/3000);
    console.log(timeoutString);
    connection.send(timeoutString);
}


function changeColourMode(){
    console.log(document.getElementById('myonoffswitch').checked);
    if (document.getElementById('myonoffswitch').checked){
        colorSet = 'M';
    }else{
        colorSet = 'S';
    }
    }


function rainbowEffect(){

   
        connection.send("R");
        document.getElementById('rainbow').style.backgroundColor = '#00878F';
        document.getElementById('r').className = 'disabled';
        document.getElementById('g').className = 'disabled';
        document.getElementById('b').className = 'disabled';
        document.getElementById('r').disabled = true;
        document.getElementById('g').disabled = true;
        document.getElementById('b').disabled = true;

        connection.send("N");
        document.getElementById('rainbow').style.backgroundColor = '#999';
        document.getElementById('r').className = 'enabled';
        document.getElementById('g').className = 'enabled';
        document.getElementById('b').className = 'enabled';
        document.getElementById('r').disabled = false;
        document.getElementById('g').disabled = false;
        document.getElementById('b').disabled = false;
        sendRGB();
    
}
