Add-Type -AssemblyName System.Drawing
if (!("GiImageMetrics" -as [type]))
{
	$drawingAssemblies = @('System.Drawing')
	if ($PSVersionTable.PSEdition -eq 'Core')
	{
		$drawingAssemblies += 'System.Drawing.Primitives'
		$drawingAssemblies += [Drawing.Bitmap].Assembly.Location
		$drawingAssemblies += @(Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows*.dll' | Select-Object -ExpandProperty FullName)
	}
	Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Drawing;
public static class GiImageMetrics
{
	static double Linear(byte value)
	{
		double s = value / 255.0;
		double mapped = s <= 0.04045 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
		return mapped / Math.Max(1e-6, 1 - mapped);
	}
	public static double[] Read(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new double[8];
			int count = 0;
			for (int y = 0; y < bitmap.Height; y += 4)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; x += 4)
				{
					Color p = bitmap.GetPixel(x, y);
					result[0] += Linear(p.R);
					result[1] += Linear(p.G);
					result[2] += Linear(p.B);
					if (p.G > 150 && p.R < 10)
					{
						result[4]++;
					}
					if (p.B > 200 && p.G > 150 && p.R < 10)
					{
						result[5]++;
					}
					if (p.R > 100 && p.G > 30 && p.G < 100 && p.B < 10)
					{
						result[6]++;
					}
					if (Math.Max(p.R, Math.Max(p.G, p.B)) > 3)
					{
						result[7]++;
					}
					count++;
				}
			}
			for (int channel = 0; channel < 3; channel++)
			{
				result[channel] /= count;
			}
			result[3] = .2126 * result[0] + .7152 * result[1] + .0722 * result[2];
			return result;
		}
	}
	public static int CheckReplay(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			int continued = 0;
			for (int y = 0; y < bitmap.Height; ++y)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; ++x)
				{
					Color p = bitmap.GetPixel(x, y);
					if (p.R != 0 || p.G != 255 || (p.B != 0 && p.B != 255))
					{
						throw new Exception("Invalid replay result at " + x + ", " + y);
					}
					if (p.B == 255)
					{
						++continued;
					}
				}
			}
			return continued;
		}
	}
	public static int MaxDifference(string referencePath, string samplePath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		{
			int error = 0;
			for (int y = 0; y < reference.Height; ++y)
			{
				for (int x = (int)(reference.Width * .65); x < reference.Width; ++x)
				{
					Color a = reference.GetPixel(x, y), b = sample.GetPixel(x, y);
					error = Math.Max(error, Math.Max(Math.Abs(a.R - b.R), Math.Max(Math.Abs(a.G - b.G), Math.Abs(a.B - b.B))));
				}
			}
			return error;
		}
	}
	public static double Mse(string referencePath, string samplePath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		{
			double error = 0;
			int count = 0;
			for (int y = 0; y < reference.Height; y += 4)
			{
				for (int x = (int)(reference.Width * .65); x < reference.Width; x += 4)
				{
					Color r = reference.GetPixel(x,y), s = sample.GetPixel(x,y);
					double a = .2126 * Linear(r.R) + .7152 * Linear(r.G) + .0722 * Linear(r.B);
					double b = .2126 * Linear(s.R) + .7152 * Linear(s.G) + .0722 * Linear(s.B);
					if (a < .0003)
					{
						continue;
					}
					error += (a-b)*(a-b);
					count++;
				}
			}
			if (count < 100)
			{
				throw new Exception("Empty diffuse-GI reference");
			}
			return error / count;
		}
	}
}
'@
}
