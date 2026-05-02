# Temu garden lights hack

I bought 12V-24V outdoor garden lights on Temu. Originally, they had two wires and a remote controller. I modified them to use a custom three-wire bus (0V, data, 24V) to control them from my smart home. 

The label said:

<table border="0">
  <tr>
    <td valign="top">
      <img src="label.jpg" alt="Product Label" width="300" />
    </td>
    <td valign="top">
      <ul>
        <li><strong>Product Name:</strong> Low Voltage LED Lights</li>
        <li><strong>Model:</strong> CTY-10W</li>
        <li><strong>Serial Number / Seri Numarasi:</strong> RGDC50245</li>
        <li><strong>Manufacturer:</strong> Shenzhen Changtaiying Technology Co., Ltd</li>
        <li><strong>Address:</strong> No. 4 Yintian Road, Bao'an District, Shenzhen City, Guangdong province, China.</li>
      </ul>
    </td>
  </tr>
</table>

Originally, the lamp had an FMD FT60E122 microcontroller in an SO-14 package with the following pinout:

<table border="0">
  <tr>
    <td>
      <!-- Bal oldali oszlop: A táblázat -->
      <table>
        <thead>
          <tr><th>#</th><th>Function</th></tr>
        </thead>
        <tbody>
          <tr><td>1</td><td>VDD (5V)</td></tr>
          <tr><td>2</td><td>Crystal1 (16 MHz)</td></tr>
          <tr><td>3</td><td>Crystal2</td></tr>
          <tr><td>4</td><td>radio recv</td></tr>
          <tr><td>5</td><td>Red PWM 62.8μs (10μs max)</td></tr>
          <tr><td>6</td><td>White PWM 62.8μs (49.2μs max)</td></tr>
          <tr><td>7</td><td>not connected</td></tr>
          <tr><td>8</td><td>Green PWM 62.8μs (49.2μs max)</td></tr>
          <tr><td>9</td><td>not connected</td></tr>
          <tr><td>10</td><td>not connected</td></tr>
          <tr><td>11</td><td>Blue PWM 62.8μs (49.2μs max)</td></tr>
          <tr><td>12</td><td>test point 2</td></tr>
          <tr><td>13</td><td>test point 1</td></tr>
          <tr><td>14</td><td>GND</td></tr>
        </tbody>
      </table>
    </td>
    <td valign="top">
      <!-- Jobb oldali oszlop: A kép -->
      <img src="/pinout.jpg" alt="pinout" width="550" />
    </td>
  </tr>
</table>


