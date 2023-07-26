// terrainScale = prefs.getFloat("t_scale", 200.0);
// sensorScale = prefs.getFloat("g_scale", 2.0);
// ballMass = prefs.getFloat("b_mass", 1.0);
// terrainFriction = prefs.getFloat("t_friction", 0.1);
// worldGravity = prefs.getFloat("w_gravity", 9.98);
// ballInHoleVelocity = prefs.getFloat("b_hole", 25.0);
// hyterisisLimit = prefs.getUInt("g_hysterisis", 50);

const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE html> 
  <html>
    <head><meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
      <title>Golf</title>
      <style>
       html {font-family: Arial; display: inline-block; text-align: center;}
        h2 {font-size: 2.3rem;}
        p {font-size: 1.9rem;}
        body {max-width: 400px; margin:0px auto; padding-bottom: 25px;}
        .slider { 
          -webkit-appearance: none; margin: 14px; width: 360px; height: 25px; background: #FFD65C;
          outline: none; -webkit-transition: .2s; transition: opacity .2s;
        }
        .slider::-webkit-slider-thumb {
          -webkit-appearance: none; appearance: none; width: 35px; height: 35px; background: #003249; cursor: pointer;
        }
        .slider::-moz-range-thumb {
          width: 35px; height: 35px; background: #003249; cursor: pointer;
        }

        .button {
          margin-top:35px;
          background-color: #FFD65C;
          border: none;
          padding: 15px 32px;
          text-align: center;
          text-decoration: none;
          display: inline-block;
          font-size: 16px;
        }
      </style>
    </head>
    <body>
      <h1>Golf Wobbler Settings</h1>
      <form>
        <div><input class="slider" type="range" id="t_scale" name="t_scale" min="0.0" max="200" step="1.0" value="%T_SCALE%"><label for="t_scale">Terrain Scale: %T_SCALE%</label></div>
        <div><input class="slider" type="range" id="g_scale" name="g_scale" min="0" max="10" step="0.1" value="%G_SCALE%"><label for="g_scale">Sensor Scale: %G_SCALE%</label></div>
        <div><input class="slider" type="range" id="b_mass" name="b_mass" min="0" max="10" step="0.1" value="%B_MASS%"><label for="b_mass">Ball Mass: %B_MASS%</label></div>
        <div><input class="slider" type="range" id="t_friction" name="t_friction" min="0" max="10" step="0.01" value="%T_FRICTION%"><label for="t_friction">Terrain Friction: %T_FRICTION%</label></div>
        <div><input class="slider" type="range" id="w_gravity" name="w_gravity" min="0" max="50" step="0.1" value="%W_GRAVITY%"><label for="w_gravity">World Gravity: %W_GRAVITY%</label></div>
        <div><input class="slider" type="range" id="b_hole" name="b_hole" min="0" max="10" step="0.1" value="%B_HOLE%"><label for="b_hole">Hole Friction: %B_HOLE%</label></div>
        <div><input class="slider" type="range" id="g_hysterisis" name="g_hysterisis" min="0" max="500" value="%G_HYSTERISIS%"><label for="g_hysterisis">Hysterisis Limit: %G_HYSTERISIS%</label></div>
        <div><input class="button" type="button" id="debug" name="debug" value="debug"></div>
      </form>
      <script>
        let t_scale = document.getElementById('t_scale')
        let t_scaleLabel = document.querySelector('label[for="t_scale"]')
        t_scale.oninput = function() {
          t_scaleLabel.innerHTML = 'Terrain Scale: ' + this.value
        }
        t_scale.onchange = function() {
          t_scaleLabel.innerHTML = 'Terrain Scale: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(t_scale.value)
          xhr.open("GET", "/pref?t_scale="+this.value, true)
          xhr.send();
        }

        let g_scale = document.getElementById('g_scale')
        let g_scaleLabel = document.querySelector('label[for="g_scale"]')
        g_scale.oninput = function() {
          g_scaleLabel.innerHTML = 'Sensor Scale: ' + this.value
        }
        g_scale.onchange = function() {
          g_scaleLabel.innerHTML = 'Sensor Scale: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(this.value)
          xhr.open("GET", "/pref?g_scale="+this.value, true)
          xhr.send();
        }

        let b_mass = document.getElementById('b_mass')
        let b_massLabel = document.querySelector('label[for="b_mass"]')
        b_mass.oninput = function() {
          b_massLabel.innerHTML = 'Ball Mass: ' + this.value
        }
        b_mass.onchange = function() {
          b_massLabel.innerHTML = 'Ball Mass: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(b_mass.value)
          xhr.open("GET", "/pref?b_mass="+this.value, true)
          xhr.send();
        }

        let t_friction = document.getElementById('t_friction')
        let t_frictionLabel = document.querySelector('label[for="t_friction"]')
        t_friction.oninput = function() {
          t_frictionLabel.innerHTML = 'Terrain Friction: ' + this.value
        }
        t_friction.onchange = function() {
          t_frictionLabel.innerHTML = 'Terrain Friction: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(t_friction.value)
          xhr.open("GET", "/pref?t_friction="+this.value, true)
          xhr.send();
        }

        let w_gravity = document.getElementById('w_gravity')
        let w_gravityLabel = document.querySelector('label[for="w_gravity"]')
        w_gravity.oninput = function() {
          w_gravityLabel.innerHTML = 'World Gravity: ' + this.value
        }
        w_gravity.onchange = function() {
          w_gravityLabel.innerHTML = 'World Gravity: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(w_gravity.value)
          xhr.open("GET", "/pref?w_gravity="+this.value, true)
          xhr.send();
        }

        let b_hole = document.getElementById('b_hole')
        let b_holeLabel = document.querySelector('label[for="b_hole"]')
        b_hole.oninput = function() {
          b_holeLabel.innerHTML = 'Hole Friction: ' + this.value
        }
        b_hole.onchange = function() {
          b_holeLabel.innerHTML = 'Hole Friction: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(b_hole.value)
          xhr.open("GET", "/pref?b_hole="+this.value, true)
          xhr.send();
        }

        let g_hysterisis = document.getElementById('g_hysterisis')
        let g_hysterisisLabel = document.querySelector('label[for="g_hysterisis"]')
        g_hysterisis.oninput = function() {
          g_hysterisisLabel.innerHTML = 'Hysterisis Limit: ' + this.value
        }
        g_hysterisis.onchange = function() {
          g_hysterisisLabel.innerHTML = 'Hysterisis Limit: ' + this.value
          var xhr = new XMLHttpRequest()
          console.log(g_hysterisis.value)
          xhr.open("GET", "/pref?g_hysterisis="+this.value, true)
          xhr.send();
        }

        let debug = document.getElementById('debug')
        debug.onclick = function() {
          var xhr = new XMLHttpRequest()
          xhr.open("GET", "/debug", true)
          xhr.send();
        }

      </script>
    </body>
  </html>
)rawliteral";


const char editor_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE html> 
  <html>
    <head><meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
      <title>Golf</title>
      <style>
       html {font-family: Arial; display: inline-block; text-align: center;}
        h2 {font-size: 2.3rem;}
        p {font-size: 1.9rem;}
        body {max-width: 400px; margin:0px auto; padding-bottom: 25px;}
        .slider { 
          -webkit-appearance: none; margin: 14px; width: 360px; height: 25px; background: #FFD65C;
          outline: none; -webkit-transition: .2s; transition: opacity .2s;
        }
        .slider::-webkit-slider-thumb {
          -webkit-appearance: none; appearance: none; width: 35px; height: 35px; background: #003249; cursor: pointer;
        }
        .slider::-moz-range-thumb {
          width: 35px; height: 35px; background: #003249; cursor: pointer;
        }

        .button {
          margin-top:35px;
          background-color: #FFD65C;
          border: none;
          padding: 15px 32px;
          text-align: center;
          text-decoration: none;
          display: inline-block;
          font-size: 16px;
        }
      </style>
    </head>
    <body>
      <h1>Golf Wobbler Editor</h1>
      <form>
        <div><input class="slider" type="range" id="t_scale" name="t_scale" min="0.0" max="200" step="1.0" value="%T_SCALE%"><label for="t_scale">Terrain Scale: %T_SCALE%</label></div>
        <div><input class="slider" type="range" id="g_scale" name="g_scale" min="0" max="10" step="0.1" value="%G_SCALE%"><label for="g_scale">Sensor Scale: %G_SCALE%</label></div>
        <div><input class="slider" type="range" id="b_mass" name="b_mass" min="0" max="10" step="0.1" value="%B_MASS%"><label for="b_mass">Ball Mass: %B_MASS%</label></div>
        <div><input class="slider" type="range" id="t_friction" name="t_friction" min="0" max="10" step="0.01" value="%T_FRICTION%"><label for="t_friction">Terrain Friction: %T_FRICTION%</label></div>
        <div><input class="slider" type="range" id="w_gravity" name="w_gravity" min="0" max="50" step="0.1" value="%W_GRAVITY%"><label for="w_gravity">World Gravity: %W_GRAVITY%</label></div>
        <div><input class="slider" type="range" id="b_hole" name="b_hole" min="0" max="10" step="0.1" value="%B_HOLE%"><label for="b_hole">Hole Friction: %B_HOLE%</label></div>
        <div><input class="slider" type="range" id="g_hysterisis" name="g_hysterisis" min="0" max="500" value="%G_HYSTERISIS%"><label for="g_hysterisis">Hysterisis Limit: %G_HYSTERISIS%</label></div>
        <div><input class="button" type="button" id="debug" name="debug" value="debug"></div>
      </form>
      <script>
        
      </script>
    </body>
  </html>
)rawliteral";