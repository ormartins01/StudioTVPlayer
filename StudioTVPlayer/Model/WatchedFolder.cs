﻿using StudioTVPlayer.Helpers;
using StudioTVPlayer.Model.Args;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Xml.Serialization;

namespace StudioTVPlayer.Model
{
    public class WatchedFolder
    {
        private bool _needsInitialization;
        private string _path;
        private bool _isFilteredByDate;
        private string _filter = "*.*";
        private Wildcard[] _filterWildcards;
        private FileSystemWatcher _fs;
        private CancellationTokenSource _cancellationTokenSource;
        private DateTime _filterDate = DateTime.Today;
        private readonly List<MediaFile> _medias = new List<MediaFile>();

        [XmlAttribute]
        public string Name { get; set; }

        [XmlAttribute]
        public string Path { get => _path; set => Set(ref _path, value); }

        [XmlAttribute]
        public bool IsFilteredByDate { get => _isFilteredByDate; set => Set(ref _isFilteredByDate, value); }

        [XmlAttribute]
        public string Filter
        {
            get => _filter; 
            set
            {
                if (!Set(ref _filter, value))
                    return;
                var filterParts = value.Split(new[] { '|' }, StringSplitOptions.RemoveEmptyEntries);
                _filterWildcards = filterParts.Select(p => new Wildcard(p)).ToArray();
            }
        }

        public event EventHandler<MediaEventArgs> MediaChanged;

        public void Initialize()
        {
            if (!_needsInitialization)
                return;
            _cancellationTokenSource?.Cancel();
            _needsInitialization = false;
            _cancellationTokenSource = new CancellationTokenSource();
            _fs = new FileSystemWatcher(Path)
            {
                NotifyFilter = NotifyFilters.FileName | NotifyFilters.Size | NotifyFilters.CreationTime | NotifyFilters.LastWrite,
                InternalBufferSize = 64000 //recommended max
            };
            _fs.Error += Fs_Error;
            _fs.Created += Fs_MediaCreated;
            _fs.Deleted += Fs_MediaDeleted;
            _fs.Renamed += Fs_MediaRenamed;
            _fs.Changed += Fs_MediaChanged;
            _fs.EnableRaisingEvents = true;
            lock (((IList)_medias).SyncRoot)
            {
                _medias.ForEach(m => m.PropertyChanged -= Media_PropertyChanged);
                _medias.Clear();
                foreach (var media in Directory.EnumerateFiles(Path)
                    .Where(Accept)
                    .Select(path => new MediaFile(path)))
                {
                    _medias.Add(media);
                    media.PropertyChanged += Media_PropertyChanged;
                    AddToVerificationQueue(media);
                }
            }
        }

        public IList<MediaFile> Medias
        {
            get
            {
                lock (((IList)_medias).SyncRoot)
                    return _medias.AsReadOnly();
            }
        }

        private bool Accept(string fullPath)
        {            
            return (!IsFilteredByDate || File.GetCreationTime(fullPath).Date == _filterDate.Date)  && (_filterWildcards == null || _filterWildcards.Length == 0 || _filterWildcards.Any(w => w.IsMatch(fullPath)));
        }

 
        [XmlIgnore]
        public DateTime FilterDate
        {
            get => _filterDate;
            set
            {

                if (!Set(ref _filterDate, value))
                    return;
                _filterDate = value;
                Initialize();
            }
        }

        private void Fs_MediaCreated(object sender, FileSystemEventArgs e)
        {
            Debug.WriteLine("Media Created Notified");
            if (!Accept(e.FullPath))
                return;
            var media = AddMediaFromPath(e.FullPath);
            AddToVerificationQueue(media);
            MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Create));
        }

        private void Fs_MediaChanged(object sender, FileSystemEventArgs e)
        {
            Debug.WriteLine("Media Changed Notified");
            MediaFile media;
            lock (((IList)_medias).SyncRoot)
                media = _medias.FirstOrDefault(m => m.FullPath == e.FullPath);
            if (media == null)
                return;
            AddToVerificationQueue(media);
            MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Change));
        }

        private void Fs_MediaRenamed(object sender, RenamedEventArgs e)
        {
            Debug.WriteLine("Media Renamed Notified");
            MediaFile media;
            lock (((IList)_medias).SyncRoot)
                media = _medias.FirstOrDefault(m => m.FullPath == e.OldFullPath);
            if (media == null && Accept(e.FullPath))
            {
                media = AddMediaFromPath(e.FullPath);
                AddToVerificationQueue(media);
                MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Create));
            }
            else if (media != null && !Accept(e.FullPath))
            {
                lock (((IList)_medias).SyncRoot)
                    _medias.Remove(media);
                media.PropertyChanged -= Media_PropertyChanged;
                MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Delete));
            }
            else if (media != null)
            {
                media.Refresh();
                AddToVerificationQueue(media);
                MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Change));
            }
        }

        private MediaFile AddMediaFromPath(string fullPath)
        {
            var media = new MediaFile(fullPath);
            lock (((IList)_medias).SyncRoot)
                _medias.Add(media);
            media.PropertyChanged += Media_PropertyChanged;
            return media;
        }

        private void Media_PropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if(e.PropertyName == nameof(MediaFile.Duration))
                MediaChanged?.Invoke(this, new MediaEventArgs((MediaFile)sender, MediaEventKind.Change));
        }

        private void Fs_MediaDeleted(object sender, FileSystemEventArgs e)
        {
            Debug.WriteLine("Media Delete Notified");
            MediaFile media;
            lock (((IList)_medias).SyncRoot)
            {
                media = _medias.FirstOrDefault(m => m.FullPath == e.FullPath);
                _medias.Remove(media);
            }
            if (media != null)
            {
                media.PropertyChanged -= Media_PropertyChanged;
                MediaChanged?.Invoke(this, new MediaEventArgs(media, MediaEventKind.Delete));
            }
        }

        private void Fs_Error(object sender, ErrorEventArgs e)
        {
            Debug.WriteLine($"Watcher error: {e}");
        }

        private bool Set<T>(ref T field, T value)
        {
            if (EqualityComparer<T>.Default.Equals(field, value))
                return false;
            field = value;
            _needsInitialization = true;
            return true;
        }

        private void AddToVerificationQueue(MediaFile media)
        {
            MediaVerifier.Current.Queue(media, _cancellationTokenSource.Token);
        }

    }
}
